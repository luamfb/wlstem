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

    // Count the number of new windows we are resizing, and how much space
    // is currently occupied
    int new_children = 0;
    double current_width_fraction = 0;
    for (int i = 0; i < children->length; ++i) {
        struct sway_container *child = children->items[i];
        current_width_fraction += child->width_fraction;
        if (child->width_fraction <= 0) {
            new_children += 1;
        }
    }

    // Calculate each height fraction
    double total_width_fraction = 0;
    for (int i = 0; i < children->length; ++i) {
        struct sway_container *child = children->items[i];
        if (child->width_fraction <= 0) {
            if (current_width_fraction <= 0) {
                child->width_fraction = 1.0;
            } else if (children->length > new_children) {
                child->width_fraction = current_width_fraction /
                    (children->length - new_children);
            } else {
                child->width_fraction = current_width_fraction;
            }
        }
        total_width_fraction += child->width_fraction;
    }
    // Normalize width fractions so the sum is 1.0
    for (int i = 0; i < children->length; ++i) {
        struct sway_container *child = children->items[i];
        child->width_fraction /= total_width_fraction;
    }

    double child_total_width = parent->width;

    // Resize windows
    sway_log(SWAY_DEBUG, "Arranging %p horizontally", parent);
    double child_x = parent->x;
    for (int i = 0; i < children->length; ++i) {
        struct sway_container *child = children->items[i];
        child->child_total_width = child_total_width;
        child->x = child_x;
        child->y = parent->y;
        child->width = round(child->width_fraction * child_total_width);
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

void arrange_workspace(struct sway_workspace *workspace) {
    if (!workspace->output) {
        // Happens when there are no outputs connected
        return;
    }
    struct sway_output *output = workspace->output;
    struct wlr_box *area = &output->usable_area;
    sway_log(SWAY_DEBUG, "Usable area for ws: %dx%d@%d,%d",
            area->width, area->height, area->x, area->y);

    workspace->width = area->width;
    workspace->height = area->height;
    workspace->x = output->lx + area->x;
    workspace->y = output->ly + area->y;

    node_set_dirty(&workspace->node);
    sway_log(SWAY_DEBUG, "Arranging workspace '%s' at %f, %f", workspace->name,
            workspace->x, workspace->y);
    struct wlr_box box;
    workspace_get_box(workspace, &box);
    arrange_children(workspace->tiling, &box);
}

void arrange_output(struct sway_output *output) {
    const struct wlr_box *output_box = wlr_output_layout_get_box(
            root->output_layout, output->wlr_output);
    output->lx = output_box->x;
    output->ly = output_box->y;
    output->width = output_box->width;
    output->height = output_box->height;

    for (int i = 0; i < output->workspaces->length; ++i) {
        struct sway_workspace *workspace = output->workspaces->items[i];
        arrange_workspace(workspace);
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
        arrange_workspace(node->sway_workspace);
        break;
    case N_CONTAINER:
        arrange_container(node->sway_container);
        break;
    }
}
