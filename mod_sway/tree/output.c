#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_damage.h>
#include "sway/layers.h"
#include "output.h"
#include "sway/tree/arrange.h"
#include "sway/input/seat.h"
#include "log.h"
#include "util.h"
#include "wlstem.h"

static void output_seize_containers_from(struct sway_output *absorber,
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

static void seize_containers_from_noop_output(struct sway_output *output) {
    if (wls->root->noop_output->active) {
        output_seize_containers_from(output, wls->root->noop_output);
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

    wl_list_insert(&wls->root->all_outputs, &output->link);

    output->active = false;

    size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
    for (size_t i = 0; i < len; ++i) {
        wl_list_init(&output->layers[i]);
    }

    return output;
}

void output_enable(struct sway_output *output) {
    if (!sway_assert(!output->enabled, "output is already enabled")) {
        return;
    }
    output->tiling = create_list();
    output->enabled = true;
    list_add(wls->root->outputs, output);

    seize_containers_from_noop_output(output);

    if (!output->active) {
        sway_log(SWAY_DEBUG, "Activating output '%s'", output->wlr_output->name);
        output->active = true;

        // Set each seat's focus if not already set
        struct sway_seat *seat = NULL;
        wl_list_for_each(seat, &server.input->seats, link) {
            if (!seat->has_focus) {
                seat_set_focus_output(seat, output);
            }
        }
    }

    input_manager_configure_xcursor();

    wl_signal_emit(&wls->node_manager->events.new_node, &output->node);

    arrange_layers(output);
    arrange_root();
}

static void output_evacuate(struct sway_output *output) {
    if (!output->active) {
        return;
    }
    struct sway_output *fallback_output = NULL;
    if (wls->root->outputs->length > 1) {
        fallback_output = wls->root->outputs->items[0];
        if (fallback_output == output) {
            fallback_output = wls->root->outputs->items[1];
        }
    }

    if (output->active) {
        struct sway_output *new_output = fallback_output;
        if (!new_output) {
            new_output = wls->root->noop_output;
        }

        if (output_has_containers(output)) {
            output_seize_containers_from(new_output, output);
        }
        output->active = false;
        node_set_dirty(&output->node);
    }
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

static void untrack_output(struct sway_container *con, void *data) {
    struct sway_output *output = data;
    int index = list_find(con->outputs, output);
    if (index != -1) {
        list_del(con->outputs, index);
    }
}

static void remove_output_from_all_focus_stacks(struct sway_output *output) {
    struct sway_seat *seat = NULL;
    wl_list_for_each(seat, &server.input->seats, link) {
        remove_node_from_focus_stack(seat, &output->node);
    }
}

void output_disable(struct sway_output *output) {
    if (!sway_assert(output->enabled, "Expected an enabled output")) {
        return;
    }
    int index = list_find(wls->root->outputs, output);
    if (!sway_assert(index >= 0, "Output not found in root node")) {
        return;
    }

    sway_log(SWAY_DEBUG, "Disabling output '%s'", output->wlr_output->name);
    wl_signal_emit(&output->events.destroy, output);

    output_evacuate(output);
    remove_output_from_all_focus_stacks(output);

    root_for_each_container(untrack_output, output);

    list_del(wls->root->outputs, index);

    output->enabled = false;
    output->current_mode = NULL;

    arrange_root();

    // Reconfigure all devices, since devices with map_to_output directives for
    // an output that goes offline should stop sending events as long as the
    // output remains offline.
    input_manager_configure_all_inputs();
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
        wlr_output_layout_get_box(wls->root->output_layout, reference->wlr_output);
    int lx = output_box->x + output_box->width / 2;
    int ly = output_box->y + output_box->height / 2;
    struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
            wls->root->output_layout, direction, reference->wlr_output, lx, ly);
    if (!wlr_adjacent) {
        return NULL;
    }
    return output_from_wlr_output(wlr_adjacent);
}

void output_for_each_container(struct sway_output *output,
        void (*f)(struct sway_container *con, void *data), void *data) {
    if (output->active) {
        for (int i = 0; i < output->tiling->length; ++i) {
            struct sway_container *container = output->tiling->items[i];
            f(container, data);
        }
    }
}

void output_get_box(struct sway_output *output, struct wlr_box *box) {
    box->x = output->lx;
    box->y = output->ly;
    box->width = output->width;
    box->height = output->height;
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

void root_for_each_container(void (*f)(struct sway_container *con, void *data),
        void *data) {
    for (int i = 0; i < wls->root->outputs->length; ++i) {
        struct sway_output *output = wls->root->outputs->items[i];
        output_for_each_container(output, f, data);
    }

    if (wls->root->noop_output->active) {
        output_for_each_container(wls->root->noop_output, f, data);
    }
}
