#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/output.h"
#include "sway/tree/workspace.h"
#include "sway/tree/view.h"
#include "list.h"
#include "log.h"

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
            root->output_layout, output->wlr_output);
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

    if (output->active_workspace) {
        struct sway_workspace *ws = output->active_workspace;
        struct wlr_box box;

        output_get_render_box(output, &box);
        arrange_children(ws->tiling, &box);

        node_set_dirty(&ws->node);
    }

}

void arrange_root(void) {
    const struct wlr_box *layout_box =
        wlr_output_layout_get_box(root->output_layout, NULL);
    root->x = layout_box->x;
    root->y = layout_box->y;
    root->width = layout_box->width;
    root->height = layout_box->height;

    for (int i = 0; i < root->outputs->length; ++i) {
        struct sway_output *output = root->outputs->items[i];
        arrange_output(output);
    }
}

void arrange_node(struct sway_node *node) {
    switch (node->type) {
    case N_ROOT:
        arrange_root();
        break;
    case N_OUTPUT:
        arrange_output(node->sway_output);
        break;
    case N_WORKSPACE:
        arrange_output(node->sway_workspace->output);
        break;
    case N_CONTAINER:
        arrange_container(node->sway_container);
        break;
    }
}
