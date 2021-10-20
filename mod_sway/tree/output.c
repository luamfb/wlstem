#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_damage.h>
#include "foreach.h"
#include "output.h"
#include "seat.h"
#include "sway_server.h"
#include "log.h"
#include "util.h"
#include "wlstem.h"

void output_seize_containers_from(struct sway_output *absorber,
    struct sway_output *giver)
{
    if (!sway_assert(absorber->active, "Expected active output")) {
        assert(false);
    }
    while (giver->tiling->length) {
        struct sway_container *container = giver->tiling->items[0];
        output_add_container(absorber, container);
    }

    node_set_dirty(&absorber->node);
}

void seize_containers_from_noop_output(struct sway_output *output) {
    if (wls->output_manager->noop_output->active) {
        output_seize_containers_from(output, wls->output_manager->noop_output);
    }
}

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

static void output_evacuate(struct sway_output *output) {
    if (!output->active) {
        return;
    }
    struct sway_output *fallback_output = NULL;
    if (wls->output_manager->outputs->length > 1) {
        fallback_output = wls->output_manager->outputs->items[0];
        if (fallback_output == output) {
            fallback_output = wls->output_manager->outputs->items[1];
        }
    }

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

struct sway_container *output_add_container(struct sway_output *output,
        struct sway_container *con) {
    if (con->output) {
        container_detach(con);
    }
    list_add(output->tiling, con);
    con->output = output;
    node_set_dirty(&con->node);
    return con;
}

bool output_has_containers(struct sway_output *output) {
    return output->tiling->length;
}
