#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
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

void output_seize_containers_from(struct sway_output *absorber,
    struct sway_output *giver)
{
    if (!sway_assert(absorber->active, "Expected active output")) {
        assert(false);
    }
    while (giver->windows->length) {
        struct sway_container *container = giver->windows->items[0];
        output_add_container(absorber, container);
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

        if (output_has_containers(output)) {
            output_seize_containers_from(new_output, output);
        }
        output->active = false;
        node_set_dirty(&output->node);
    }
}

static void untrack_output(struct sway_container *con, void *data) {
    struct sway_output *output = data;
    int index = list_find(con->outputs, output);
    if (index != -1) {
        list_del(con->outputs, index);
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

    wls_output_layout_for_each_container(untrack_output, output);

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

struct sway_container *output_add_container(struct sway_output *output,
        struct sway_container *con) {
    if (con->output) {
        container_detach(con);
    }
    list_add(output->windows, con);
    con->output = output;
    node_set_dirty(&con->node);
    return con;
}

bool output_has_containers(struct sway_output *output) {
    return output->windows->length;
}
