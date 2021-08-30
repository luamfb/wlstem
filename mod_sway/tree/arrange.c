#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/desktop/transaction.h"
#include "sway/input/seat.h"
#include "sway/tree/arrange.h"
#include "container.h"
#include "output.h"
#include "sway/tree/view.h"
#include "list.h"
#include "log.h"
#include "wlstem.h"

static void wm_handle_output_layout_change(
        struct wl_listener *listener, void *data) {
    arrange_output_layout();
}

static void wm_handle_output_connected(
        struct wl_listener *listener, void *data) {
    struct sway_output *output = data;
    seize_containers_from_noop_output(output);

    // Set each seat's focus if not already set
    struct sway_seat *seat = NULL;
    wl_list_for_each(seat, &server.input->seats, link) {
        if (!seat->has_focus) {
            seat_set_focus_output(seat, output);
        }
    }

    arrange_root();
}

static void wm_handle_output_disconnected(
        struct wl_listener *listener, void *data) {
    arrange_root();
}

struct server_wm * server_wm_create(void) {
    struct server_wm *wm = calloc(1, sizeof(struct server_wm));
    if (!wm) {
        return NULL;
    }
    wl_signal_add(&wls->root->events.output_layout_changed,
        &wm->output_layout_change);
    wm->output_layout_change.notify = wm_handle_output_layout_change;

    wl_signal_add(&wls->root->events.output_connected,
        &wm->output_connected);
    wm->output_connected.notify = wm_handle_output_connected;

    wl_signal_add(&wls->root->events.output_disconnected,
        &wm->output_disconnected);
    wm->output_disconnected.notify = wm_handle_output_disconnected;

    return wm;
}

void server_wm_destroy(struct server_wm *wm) {
    if (!wm) {
        return;
    }
    free(wm);
}

static void apply_horiz_layout(list_t *children, struct wlr_box *parent) {
    if (!children->length) {
        return;
    }

    double child_total_width = parent->width;
    double width_fraction = 1.0 / (double)children->length;

    // Resize windows
    sway_log(SWAY_DEBUG, "Arranging %p horizontally", parent);
    double child_x = parent->x;
    for (int i = 0; i < children->length; ++i) {
        struct sway_container *child = children->items[i];
        child->x = child_x;
        child->y = parent->y;
        child->width = round(width_fraction * child_total_width);
        child->height = parent->height;
        child_x += child->width;

        // Make last child use remaining width of parent
        if (i == children->length - 1) {
            child->width = parent->x + parent->width - child->x;
        }
    }
}

static void arrange_children(list_t *children, struct wlr_box *parent) {

    apply_horiz_layout(children, parent);

    // Recurse into child containers
    for (int i = 0; i < children->length; ++i) {
        struct sway_container *child = children->items[i];
        arrange_container(child);
    }
}

void arrange_container(struct sway_container *container) {
    if (container->view) {
        view_autoconfigure(container->view);
        node_set_dirty(&container->node);
        return;
    }
    struct wlr_box box;
    container_get_box(container, &box);
    node_set_dirty(&container->node);
}

void arrange_output(struct sway_output *output) {
    const struct wlr_box *output_box = wlr_output_layout_get_box(
            wls->root->output_layout, output->wlr_output);
    output->lx = output_box->x;
    output->ly = output_box->y;
    output->width = output_box->width;
    output->height = output_box->height;

    struct wlr_box *area = &output->usable_area;
    sway_log(SWAY_DEBUG, "output usable area: %dx%d@%d,%d",
        area->width, area->height, area->x, area->y);

    output->render_lx = output->lx + area->x;
    output->render_ly = output->ly + area->y;

    sway_log(SWAY_DEBUG, "Arranging renderview of output %s at %d, %d",
        output->wlr_output->name, output->render_lx, output->render_ly);

    if (output->active) {
        struct wlr_box box;

        output_get_render_box(output, &box);
        arrange_children(output->tiling, &box);

        node_set_dirty(&output->node);
    }

}

void arrange_root(void) {
    const struct wlr_box *layout_box =
        wlr_output_layout_get_box(wls->root->output_layout, NULL);
    wls->root->x = layout_box->x;
    wls->root->y = layout_box->y;
    wls->root->width = layout_box->width;
    wls->root->height = layout_box->height;

    for (int i = 0; i < wls->root->outputs->length; ++i) {
        struct sway_output *output = wls->root->outputs->items[i];
        arrange_output(output);
    }
}

void arrange_output_layout(void) {
    arrange_root();
    transaction_commit_dirty();
}
