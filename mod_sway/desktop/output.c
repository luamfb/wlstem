#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/region.h>
#include "config.h"
#include "log.h"
#include "sway_config.h"
#include "transaction.h"
#include "input_manager.h"
#include "foreach.h"
#include "seat.h"
#include "layers.h"
#include "output.h"
#include "sway_server.h"
#include "sway_surface.h"
#include "server_wm.h"
#include "container.h"
#include "output_manager.h"
#include "view.h"
#include "wlstem.h"
#include "server.h"

struct sway_output *output_by_name_or_id(const char *name_or_id) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        if (strcasecmp(identifier, name_or_id) == 0
                || strcasecmp(output->wlr_output->name, name_or_id) == 0) {
            return output;
        }
    }
    return NULL;
}

struct sway_output *all_output_by_name_or_id(const char *name_or_id) {
    struct sway_output *output;
    wl_list_for_each(output, &wls->output_manager->all_outputs, link) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        if (strcasecmp(identifier, name_or_id) == 0
                || strcasecmp(output->wlr_output->name, name_or_id) == 0) {
            return output;
        }
    }
    return NULL;
}

struct send_frame_done_data {
    struct timespec when;
    int msec_until_refresh;
};

static void send_frame_done_iterator(struct sway_output *output, struct sway_view *view,
        struct wlr_surface *surface, struct wlr_box *box, float rotation,
        void *user_data) {
    int view_max_render_time = 0;
    if (view != NULL) {
        view_max_render_time = view->max_render_time;
    }

    struct send_frame_done_data *data = user_data;

    int delay = data->msec_until_refresh - output->max_render_time
            - view_max_render_time;

    if (output->max_render_time == 0 || view_max_render_time == 0 || delay < 1) {
        wlr_surface_send_frame_done(surface, &data->when);
    } else {
        struct sway_surface *sway_surface = surface->data;
        wl_event_source_timer_update(sway_surface->frame_done_timer, delay);
    }
}

static void send_frame_done(struct sway_output *output, struct send_frame_done_data *data) {
    output_for_each_surface(output, send_frame_done_iterator, data);
}

static int output_repaint_timer_handler(void *data) {
    struct sway_output *output = data;
    if (output->wlr_output == NULL) {
        return 0;
    }

    output->wlr_output->frame_pending = false;

    if (!output->current.active) {
        return 0;
    }

    bool needs_frame;
    pixman_region32_t damage;
    pixman_region32_init(&damage);
    if (!wlr_output_damage_attach_render(output->damage,
            &needs_frame, &damage)) {
        return 0;
    }

    if (needs_frame) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        output_render(output, &now, &damage);
    } else {
        wlr_output_rollback(output->wlr_output);
    }

    pixman_region32_fini(&damage);

    return 0;
}

static void damage_handle_frame(struct wl_listener *listener, void *user_data) {
    struct sway_output *output =
        wl_container_of(listener, output, damage_frame);
    if (!output->enabled || !output->wlr_output->enabled) {
        return;
    }

    // Compute predicted milliseconds until the next refresh. It's used for
    // delaying both output rendering and surface frame callbacks.
    int msec_until_refresh = 0;

    if (output->max_render_time != 0) {
        struct timespec now;
        clockid_t presentation_clock
            = wlr_backend_get_presentation_clock(wls->server->backend);
        clock_gettime(presentation_clock, &now);

        const long NSEC_IN_SECONDS = 1000000000;
        struct timespec predicted_refresh = output->last_presentation;
        predicted_refresh.tv_nsec += output->refresh_nsec % NSEC_IN_SECONDS;
        predicted_refresh.tv_sec += output->refresh_nsec / NSEC_IN_SECONDS;
        if (predicted_refresh.tv_nsec >= NSEC_IN_SECONDS) {
            predicted_refresh.tv_sec += 1;
            predicted_refresh.tv_nsec -= NSEC_IN_SECONDS;
        }

        // If the predicted refresh time is before the current time then
        // there's no point in delaying.
        //
        // We only check tv_sec because if the predicted refresh time is less
        // than a second before the current time, then msec_until_refresh will
        // end up slightly below zero, which will effectively disable the delay
        // without potential disastrous negative overflows that could occur if
        // tv_sec was not checked.
        if (predicted_refresh.tv_sec >= now.tv_sec) {
            long nsec_until_refresh
                = (predicted_refresh.tv_sec - now.tv_sec) * NSEC_IN_SECONDS
                    + (predicted_refresh.tv_nsec - now.tv_nsec);

            // We want msec_until_refresh to be conservative, that is, floored.
            // If we have 7.9 msec until refresh, we better compute the delay
            // as if we had only 7 msec, so that we don't accidentally delay
            // more than necessary and miss a frame.
            msec_until_refresh = nsec_until_refresh / 1000000;
        }
    }

    int delay = msec_until_refresh - output->max_render_time;

    // If the delay is less than 1 millisecond (which is the least we can wait)
    // then just render right away.
    if (delay < 1) {
        output_repaint_timer_handler(output);
    } else {
        output->wlr_output->frame_pending = true;
        wl_event_source_timer_update(output->repaint_timer, delay);
    }

    // Send frame done to all visible surfaces
    struct send_frame_done_data data = {0};
    clock_gettime(CLOCK_MONOTONIC, &data.when);
    data.msec_until_refresh = msec_until_refresh;
    send_frame_done(output, &data);
}

// Expecting an unscaled box in layout coordinates
static void damage_handle_destroy(struct wl_listener *listener, void *data) {
    struct sway_output *output =
        wl_container_of(listener, output, damage_destroy);
    if (!output->enabled) {
        return;
    }
    output_disable(output);

    wl_list_remove(&output->damage_destroy.link);
    wl_list_remove(&output->damage_frame.link);

    transaction_commit_dirty();
}

static void handle_destroy(struct wl_listener *listener, void *data) {
    struct sway_output *output = wl_container_of(listener, output, destroy);
    wl_signal_emit(&output->events.destroy, output);

    if (output->enabled) {
        output_disable(output);
    }
    output_begin_destroy(output);

    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->commit.link);
    wl_list_remove(&output->mode.link);
    wl_list_remove(&output->present.link);

    transaction_commit_dirty();

    wls_update_output_manager_config(wls->output_manager);
}

static void handle_mode(struct wl_listener *listener, void *data) {
    struct sway_output *output = wl_container_of(listener, output, mode);
    if (!output->enabled && !output->enabling) {
        struct output_config *oc = find_output_config(output);
        if (output->wlr_output->current_mode != NULL &&
                (!oc || oc->enabled)) {
            // We want to enable this output, but it didn't work last time,
            // possibly because we hadn't enough CRTCs. Try again now that the
            // output has a mode.
            sway_log(SWAY_DEBUG, "Output %s has gained a CRTC, "
                "trying to enable it", output->wlr_output->name);
            apply_output_config(oc, output);
        }
        return;
    }
    if (!output->enabled) {
        return;
    }
    arrange_layers(output);
    arrange_output(output);
    transaction_commit_dirty();

    wls_update_output_manager_config(wls->output_manager);
}

static void update_textures(struct sway_container *con, void *data) {
    container_update_title_textures(con);
}

void handle_output_commit(struct sway_output *output,
        struct wlr_output_event_commit *event) {

    if (!output->enabled) {
        return;
    }

    if (event->committed & WLR_OUTPUT_STATE_SCALE) {
        output_for_each_container(output, update_textures, NULL);
    }

    if (event->committed & (WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_SCALE)) {
        arrange_layers(output);
        arrange_output(output);
        transaction_commit_dirty();

        wls_update_output_manager_config(wls->output_manager);
    }
}

static void handle_commit(struct wl_listener *listener, void *data) {
    struct sway_output *output = wl_container_of(listener, output, commit);
    struct wlr_output_event_commit *event = data;

    wls->handle_output_commit(output, event);
}

static void handle_present(struct wl_listener *listener, void *data) {
    struct sway_output *output = wl_container_of(listener, output, present);
    struct wlr_output_event_present *output_event = data;

    if (!output->enabled) {
        return;
    }

    output->last_presentation = *output_event->when;
    output->refresh_nsec = output_event->refresh;
}

void handle_new_output(struct wl_listener *listener, void *data) {
    struct sway_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;
    sway_log(SWAY_DEBUG, "New output %p: %s", wlr_output, wlr_output->name);

    struct sway_output *output = output_create(wlr_output);
    if (!output) {
        return;
    }
    output->damage = wlr_output_damage_create(wlr_output);

    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    output->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_output->events.commit, &output->commit);
    output->commit.notify = handle_commit;
    wl_signal_add(&wlr_output->events.mode, &output->mode);
    output->mode.notify = handle_mode;
    wl_signal_add(&wlr_output->events.present, &output->present);
    output->present.notify = handle_present;
    wl_signal_add(&output->damage->events.frame, &output->damage_frame);
    output->damage_frame.notify = damage_handle_frame;
    wl_signal_add(&output->damage->events.destroy, &output->damage_destroy);
    output->damage_destroy.notify = damage_handle_destroy;

    output->repaint_timer = wl_event_loop_add_timer(wls->server->wl_event_loop,
        output_repaint_timer_handler, output);

    struct output_config *oc = find_output_config(output);
    apply_output_config(oc, output);
    free_output_config(oc);

    transaction_commit_dirty();

    wls_update_output_manager_config(wls->output_manager);
}

void handle_output_layout_change(struct wl_listener *listener,
        void *data) {
    struct sway_server *server =
        wl_container_of(listener, server, output_layout_change);
    wls_update_output_manager_config(wls->output_manager);
    wl_signal_emit(&wls->output_manager->events.output_layout_changed, wls->output_manager);
}

void handle_output_power_manager_set_mode(struct wl_listener *listener,
        void *data) {
    struct wlr_output_power_v1_set_mode_event *event = data;
    struct sway_output *output = event->output->data;

    struct output_config *oc = new_output_config(output->wlr_output->name);
    switch (event->mode) {
    case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
        oc->dpms_state = DPMS_OFF;
        break;
    case ZWLR_OUTPUT_POWER_V1_MODE_ON:
        oc->dpms_state = DPMS_ON;
        break;
    }
    oc = store_output_config(oc);
    apply_output_config(oc, output);
}
