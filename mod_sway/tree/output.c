#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_damage.h>
#include "sway/layers.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/workspace.h"
#include "log.h"
#include "util.h"

static void output_seize_containers_from_workspace(
    struct sway_output *absorber,
    struct sway_workspace *giver)
{
    if (!sway_assert(absorber->active_workspace,
            "Expected output with an active workspace")) {
        assert(false);
    }
    struct sway_workspace *absorber_ws = absorber->active_workspace;
    struct sway_output *giver_output = giver->output;

    while (giver_output->tiling->length) {
        struct sway_container *container = giver_output->tiling->items[0];
        workspace_add_tiling(absorber_ws, container);
    }

    node_set_dirty(&absorber->node);
    node_set_dirty(&absorber_ws->node);
}

static void seize_containers_from_noop_output(struct sway_output *output) {
    if (root->noop_output->active_workspace) {
        struct sway_workspace *ws = root->noop_output->active_workspace;
        output_seize_containers_from_workspace(output, ws);
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

    wl_list_insert(&root->all_outputs, &output->link);

    output->active_workspace = NULL;

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
    list_add(root->outputs, output);

    seize_containers_from_noop_output(output);

    struct sway_workspace *ws = NULL;
    if (!output->active_workspace) {
        // Create workspace
        sway_log(SWAY_DEBUG, "Creating default workspace");
        ws = workspace_create(output);
        // Set each seat's focus if not already set
        struct sway_seat *seat = NULL;
        wl_list_for_each(seat, &server.input->seats, link) {
            if (!seat->has_focus) {
                seat_set_focus_workspace(seat, ws);
            }
        }
    }

    input_manager_configure_xcursor();

    wl_signal_emit(&root->events.new_node, &output->node);

    arrange_layers(output);
    arrange_root();
}

static void output_evacuate(struct sway_output *output) {
    if (!output->active_workspace) {
        return;
    }
    struct sway_output *fallback_output = NULL;
    if (root->outputs->length > 1) {
        fallback_output = root->outputs->items[0];
        if (fallback_output == output) {
            fallback_output = root->outputs->items[1];
        }
    }

    if (output->active_workspace) {
        struct sway_workspace *workspace = output->active_workspace;

        workspace_detach(workspace);

        struct sway_output *new_output = fallback_output;
        if (!new_output) {
            new_output = root->noop_output;
        }

        if (!workspace_is_empty(workspace)) {
            output_seize_containers_from_workspace(new_output, workspace);
        }
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
    if (output->active_workspace) {
        workspace_begin_destroy(output->active_workspace);
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

void output_disable(struct sway_output *output) {
    if (!sway_assert(output->enabled, "Expected an enabled output")) {
        return;
    }
    int index = list_find(root->outputs, output);
    if (!sway_assert(index >= 0, "Output not found in root node")) {
        return;
    }

    sway_log(SWAY_DEBUG, "Disabling output '%s'", output->wlr_output->name);
    wl_signal_emit(&output->events.destroy, output);

    output_evacuate(output);

    root_for_each_container(untrack_output, output);

    list_del(root->outputs, index);

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
        wlr_output_layout_get_box(root->output_layout, reference->wlr_output);
    int lx = output_box->x + output_box->width / 2;
    int ly = output_box->y + output_box->height / 2;
    struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
            root->output_layout, direction, reference->wlr_output, lx, ly);
    if (!wlr_adjacent) {
        return NULL;
    }
    return output_from_wlr_output(wlr_adjacent);
}

void output_add_workspace(struct sway_output *output,
        struct sway_workspace *workspace) {
    if (workspace->output) {
        sway_log(SWAY_ERROR,
            "tried to add workspace which is already tied to another output");
        assert(false);
    }
    if (output->active_workspace) {
        sway_log(SWAY_ERROR,
            "tried to add workspace to output which already has an active workspace");
        assert(false);
    }
    output->active_workspace = workspace;
    workspace->output = output;
    node_set_dirty(&output->node);
    node_set_dirty(&workspace->node);
}

void output_for_each_workspace(struct sway_output *output,
        void (*f)(struct sway_workspace *ws, void *data), void *data) {
    if (output->active_workspace) {
        f(output->active_workspace, data);
    }
}

void output_for_each_container(struct sway_output *output,
        void (*f)(struct sway_container *con, void *data), void *data) {
    if (output->active_workspace) {
        workspace_for_each_container(output->active_workspace, f, data);
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
