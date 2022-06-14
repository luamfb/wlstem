#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output_damage.h>
#include "foreach.h"
#include "layers.h"
#include "list.h"
#include "log.h"
#include "output.h"
#include "seat.h"
#include "wlstem.h"

struct sway_output *output_create(struct wlr_output *wlr_output) {
    struct sway_output *output = calloc(1, sizeof(struct sway_output));
    node_init(&output->node, N_OUTPUT, output);
    output->wlr_output = wlr_output;
    wlr_output->data = output;
    output->detected_subpixel = wlr_output->subpixel;
    output->scale_filter = SCALE_FILTER_NEAREST;

    wl_signal_init(&output->events.destroy);

    wl_list_insert(&wls->output_manager->all_outputs, &output->link);

    output->active = false;

    size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
    for (size_t i = 0; i < len; ++i) {
        wl_list_init(&output->layers[i]);
    }

    return output;
}

void output_begin_destroy(struct sway_output *output) {
    if (!sway_assert(!output->enabled, "Expected a disabled output")) {
        return;
    }
    sway_log(SWAY_DEBUG, "Destroying output '%s'", output->wlr_output->name);

    output->node.destroying = true;
    node_set_dirty(&output->node);

    wl_list_remove(&output->link);
    output->wlr_output->data = NULL;
    output->wlr_output = NULL;
}

void output_enable(struct sway_output *output) {
    if (!sway_assert(!output->enabled, "output is already enabled")) {
        return;
    }
    output->windows = create_list();
    output->enabled = true;
    list_add(wls->output_manager->outputs, output);

    if (!output->active) {
        sway_log(SWAY_DEBUG, "Activating output '%s'", output->wlr_output->name);
        output->active = true;
    }

    wl_signal_emit(&wls->node_manager->events.new_node, &output->node);
    wl_signal_emit(&wls->output_manager->events.output_connected, output);
}

void seize_windows_from_noop_output(struct sway_output *output) {
    if (wls->output_manager->noop_output->active) {
        output_seize_windows_from(output, wls->output_manager->noop_output);
    }
}

void output_seize_windows_from(struct sway_output *absorber,
    struct sway_output *giver)
{
    if (!sway_assert(absorber->active, "Expected active output")) {
        assert(false);
    }
    while (giver->windows->length) {
        struct wls_window *window = giver->windows->items[0];
        output_add_window(absorber, window);
    }

    node_set_dirty(&absorber->node);
}

static void output_evacuate(struct sway_output *output) {
    if (!output->active) {
        return;
    }
    struct sway_output *fallback_output =
        wls->choose_absorber_output(output);

    if (output->active) {
        struct sway_output *new_output = fallback_output;
        if (!new_output) {
            new_output = wls->output_manager->noop_output;
        }

        if (output_has_windows(output)) {
            output_seize_windows_from(new_output, output);
        }
        output->active = false;
        node_set_dirty(&output->node);
    }
}

static void untrack_output(struct wls_window *win, void *data) {
    struct sway_output *output = data;
    int index = list_find(win->outputs, output);
    if (index != -1) {
        list_del(win->outputs, index);
    }
}

static void remove_output_from_all_focus_stacks(struct sway_output *output) {
    struct sway_seat *seat = NULL;
    wl_list_for_each(seat, &wls->seats, link) {
        remove_node_from_focus_stack(seat, &output->node);
    }
}

void output_disable(struct sway_output *output) {
    if (!sway_assert(output->enabled, "Expected an enabled output")) {
        return;
    }
    int index = list_find(wls->output_manager->outputs, output);
    if (!sway_assert(index >= 0, "Output not found in root node")) {
        return;
    }

    sway_log(SWAY_DEBUG, "Disabling output '%s'", output->wlr_output->name);
    wl_signal_emit(&output->events.destroy, output);

    output_evacuate(output);
    remove_output_from_all_focus_stacks(output);

    wls_output_layout_for_each_window(untrack_output, output);

    list_del(wls->output_manager->outputs, index);

    output->enabled = false;
    output->current_mode = NULL;

    wl_signal_emit(&wls->output_manager->events.output_disconnected, output);
}


void output_destroy(struct sway_output *output) {
    if (!sway_assert(output->node.destroying,
                "Tried to free output which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(output->wlr_output == NULL,
                "Tried to free output which still had a wlr_output")) {
        return;
    }
    if (!sway_assert(output->node.ntxnrefs == 0, "Tried to free output "
                "which is still referenced by transactions")) {
        return;
    }
    wl_event_source_remove(output->repaint_timer);
    list_free(output->windows);
    list_free(output->current.windows);
    free(output);
}

void output_damage_whole(struct sway_output *output) {
    // The output can exist with no wlr_output if it's just been disconnected
    // and the transaction to evacuate it has't completed yet.
    if (output && output->wlr_output && output->damage) {
        wlr_output_damage_add_whole(output->damage);
    }
}

bool output_has_opaque_overlay_layer_surface(struct sway_output *output) {
    struct sway_layer_surface *sway_layer_surface;
    wl_list_for_each(sway_layer_surface,
            &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], link) {
        struct wlr_surface *wlr_surface = sway_layer_surface->layer_surface->surface;
        pixman_box32_t output_box = {
            .x2 = output->width,
            .y2 = output->height,
        };
        pixman_region32_t surface_opaque_box;
        pixman_region32_init(&surface_opaque_box);
        pixman_region32_copy(&surface_opaque_box, &wlr_surface->opaque_region);
        pixman_region32_translate(&surface_opaque_box,
            sway_layer_surface->geo.x, sway_layer_surface->geo.y);
        pixman_region_overlap_t contains =
            pixman_region32_contains_rectangle(&surface_opaque_box, &output_box);
        pixman_region32_fini(&surface_opaque_box);

        if (contains == PIXMAN_REGION_IN) {
            return true;
        }
    }
    return false;
}

static int scale_length(int length, int offset, float scale) {
    return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
    box->width = scale_length(box->width, box->x, scale);
    box->height = scale_length(box->height, box->y, scale);
    box->x = round(box->x * scale);
    box->y = round(box->y * scale);
}

void output_get_box(struct sway_output *output, struct wlr_box *box) {
    box->x = output->lx;
    box->y = output->ly;
    box->width = output->width;
    box->height = output->height;
}

struct wls_window *output_add_window(struct sway_output *output,
        struct wls_window *win) {
    if (win->output) {
        window_detach(win);
    }
    list_add(output->windows, win);
    win->output = output;
    node_set_dirty(&win->node);
    return win;
}

bool output_has_windows(struct sway_output *output) {
    return output->windows->length;
}

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

struct sway_output *output_from_wlr_output(struct wlr_output *output) {
    return output->data;
}

struct sway_output *output_get_in_direction(struct sway_output *reference,
        enum wlr_direction direction) {
    if (!sway_assert(direction, "got invalid direction: %d", direction)) {
        return NULL;
    }
    struct wlr_box *output_box =
        wlr_output_layout_get_box(wls->output_manager->output_layout, reference->wlr_output);
    int lx = output_box->x + output_box->width / 2;
    int ly = output_box->y + output_box->height / 2;
    struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
            wls->output_manager->output_layout, direction, reference->wlr_output, lx, ly);
    if (!wlr_adjacent) {
        return NULL;
    }
    return output_from_wlr_output(wlr_adjacent);
}

void output_get_render_box(struct sway_output *output, struct wlr_box *box) {
    box->x = output->render_lx;
    box->y = output->render_ly;
    box->width = output->usable_area.width;
    box->height = output->usable_area.height;
}
