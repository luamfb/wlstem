#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output_damage.h>
#include "layers.h"
#include "list.h"
#include "log.h"
#include "output.h"
#include "wlstem.h"

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
    output->tiling = create_list();
    output->enabled = true;
    list_add(wls->output_manager->outputs, output);

    if (!output->active) {
        sway_log(SWAY_DEBUG, "Activating output '%s'", output->wlr_output->name);
        output->active = true;
    }

    wl_signal_emit(&wls->node_manager->events.new_node, &output->node);
    wl_signal_emit(&wls->output_manager->events.output_connected, output);
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
    list_free(output->tiling);
    list_free(output->current.tiling);
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
