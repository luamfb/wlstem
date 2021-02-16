#define _POSIX_C_SOURCE 200809
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include "stringop.h"
#include "sway/input/input-manager.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct sway_output *workspace_get_initial_output(const char *name) {
    // Otherwise try to put it on the focused output
    struct sway_seat *seat = input_manager_current_seat();
    struct sway_node *focus = seat_get_focus_inactive(seat, &root->node);
    if (focus && focus->type == N_WORKSPACE) {
        return focus->sway_workspace->output;
    } else if (focus && focus->type == N_CONTAINER) {
        return focus->sway_container->workspace->output;
    }
    // Fallback to the first output or noop output for headless
    return root->outputs->length ? root->outputs->items[0] : root->noop_output;
}

struct sway_workspace *workspace_create(struct sway_output *output,
        const char *name) {
    if (output == NULL) {
        output = workspace_get_initial_output(name);
    }

    sway_log(SWAY_DEBUG, "Adding workspace %s for output %s", name,
            output->wlr_output->name);

    struct sway_workspace *ws = calloc(1, sizeof(struct sway_workspace));
    if (!ws) {
        sway_log(SWAY_ERROR, "Unable to allocate sway_workspace");
        return NULL;
    }
    node_init(&ws->node, N_WORKSPACE, ws);
    ws->name = name ? strdup(name) : NULL;
    ws->tiling = create_list();
    ws->output_priority = create_list();

    // If not already added, add the output to the lowest priority
    workspace_output_add_priority(ws, output);

    output_add_workspace(output, ws);
    output_sort_workspaces(output);

    wl_signal_emit(&root->events.new_node, &ws->node);

    return ws;
}

void workspace_destroy(struct sway_workspace *workspace) {
    if (!sway_assert(workspace->node.destroying,
                "Tried to free workspace which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(workspace->node.ntxnrefs == 0, "Tried to free workspace "
                "which is still referenced by transactions")) {
        return;
    }

    free(workspace->name);
    list_free_items_and_destroy(workspace->output_priority);
    list_free(workspace->tiling);
    list_free(workspace->current.tiling);
    free(workspace);
}

void workspace_begin_destroy(struct sway_workspace *workspace) {
    sway_log(SWAY_DEBUG, "Destroying workspace '%s'", workspace->name);
    wl_signal_emit(&workspace->node.events.destroy, &workspace->node);

    if (workspace->output) {
        workspace_detach(workspace);
    }
    workspace->node.destroying = true;
    node_set_dirty(&workspace->node);
}

void workspace_consider_destroy(struct sway_workspace *ws) {
    if (ws->tiling->length) {
        return;
    }

    if (ws->output && output_get_active_workspace(ws->output) == ws) {
        return;
    }

    struct sway_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        struct sway_node *node = seat_get_focus_inactive(seat, &root->node);
        if (node == &ws->node) {
            return;
        }
    }

    workspace_begin_destroy(ws);
}

static void workspace_name_from_binding(const struct sway_binding * binding,
        const char* output_name, int *min_order, char **earliest_name) {
}

char *workspace_next_name(const char *output_name) {
    sway_log(SWAY_DEBUG, "Workspace: Generating new workspace name for output %s",
            output_name);
    // Scan for available workspace names by looking through output-workspace
    // assignments primarily, falling back to bindings and numbers.
    struct sway_mode *mode = config->current_mode;

    char identifier[128];
    struct sway_output *output = output_by_name_or_id(output_name);
    if (!output) {
        return NULL;
    }
    output_name = output->wlr_output->name;
    output_get_identifier(identifier, sizeof(identifier), output);

    int order = INT_MAX;
    char *target = NULL;
    for (int i = 0; i < mode->keysym_bindings->length; ++i) {
        workspace_name_from_binding(mode->keysym_bindings->items[i],
                output_name, &order, &target);
    }
    for (int i = 0; i < mode->keycode_bindings->length; ++i) {
        workspace_name_from_binding(mode->keycode_bindings->items[i],
                output_name, &order, &target);
    }
    if (target != NULL) {
        return target;
    }
    // As a fall back, use the next available number
    char name[12] = "";
    unsigned int ws_num = 1;
    do {
        snprintf(name, sizeof(name), "%u", ws_num++);
    } while (workspace_by_number(name));
    return strdup(name);
}

static bool _workspace_by_number(struct sway_workspace *ws, void *data) {
    char *name = data;
    char *ws_name = ws->name;
    while (isdigit(*name)) {
        if (*name++ != *ws_name++) {
            return false;
        }
    }
    return !isdigit(*ws_name);
}

struct sway_workspace *workspace_by_number(const char* name) {
    return root_find_workspace(_workspace_by_number, (void *) name);
}

bool workspace_switch(struct sway_workspace *workspace) {
    struct sway_seat *seat = input_manager_current_seat();

    sway_log(SWAY_DEBUG, "Switching to workspace %p:%s",
        workspace, workspace->name);
    struct sway_node *next = seat_get_focus_inactive(seat, &workspace->node);
    if (next == NULL) {
        next = &workspace->node;
    }
    seat_set_focus(seat, next);
    arrange_workspace(workspace);
    return true;
}

bool workspace_is_visible(struct sway_workspace *ws) {
    if (ws->node.destroying) {
        return false;
    }
    return output_get_active_workspace(ws->output) == ws;
}

bool workspace_is_empty(struct sway_workspace *ws) {
    if (ws->tiling->length) {
        return false;
    }
    return true;
}

static int find_output(const void *id1, const void *id2) {
    return strcmp(id1, id2);
}

static int workspace_output_get_priority(struct sway_workspace *ws,
        struct sway_output *output) {
    char identifier[128];
    output_get_identifier(identifier, sizeof(identifier), output);
    int index_id = list_seq_find(ws->output_priority, find_output, identifier);
    int index_name = list_seq_find(ws->output_priority, find_output,
            output->wlr_output->name);
    return index_name < 0 || index_id < index_name ? index_id : index_name;
}

void workspace_output_add_priority(struct sway_workspace *workspace,
        struct sway_output *output) {
    if (workspace_output_get_priority(workspace, output) < 0) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        list_add(workspace->output_priority, strdup(identifier));
    }
}

struct sway_output *workspace_output_get_highest_available(
        struct sway_workspace *ws, struct sway_output *exclude) {
    char exclude_id[128] = {'\0'};
    if (exclude) {
        output_get_identifier(exclude_id, sizeof(exclude_id), exclude);
    }

    for (int i = 0; i < ws->output_priority->length; i++) {
        char *name = ws->output_priority->items[i];
        if (exclude && (strcmp(name, exclude->wlr_output->name) == 0
                    || strcmp(name, exclude_id) == 0)) {
            continue;
        }

        struct sway_output *output = output_by_name_or_id(name);
        if (output) {
            return output;
        }
    }

    return NULL;
}

void workspace_for_each_container(struct sway_workspace *ws,
        void (*f)(struct sway_container *con, void *data), void *data) {
    // Tiling
    for (int i = 0; i < ws->tiling->length; ++i) {
        struct sway_container *container = ws->tiling->items[i];
        f(container, data);
        container_for_each_child(container, f, data);
    }
}

struct sway_container *workspace_find_container(struct sway_workspace *ws,
        bool (*test)(struct sway_container *con, void *data), void *data) {
    struct sway_container *result = NULL;
    // Tiling
    for (int i = 0; i < ws->tiling->length; ++i) {
        struct sway_container *child = ws->tiling->items[i];
        if (test(child, data)) {
            return child;
        }
        if ((result = container_find_child(child, test, data))) {
            return result;
        }
    }
    return NULL;
}

static void set_workspace(struct sway_container *container, void *data) {
    container->workspace = container->parent->workspace;
}

static void workspace_attach_tiling(struct sway_workspace *ws,
        struct sway_container *con) {
    list_add(ws->tiling, con);
    con->workspace = ws;
    container_for_each_child(con, set_workspace, NULL);
    node_set_dirty(&ws->node);
    node_set_dirty(&con->node);
}

struct sway_container *workspace_wrap_children(struct sway_workspace *ws) {
    struct sway_container *middle = container_create(NULL);
    while (ws->tiling->length) {
        struct sway_container *child = ws->tiling->items[0];
        container_detach(child);
        container_add_child(middle, child);
    }
    workspace_attach_tiling(ws, middle);
    return middle;
}

void workspace_unwrap_children(struct sway_workspace *ws,
        struct sway_container *wrap) {
    if (!sway_assert(workspace_is_empty(ws),
            "target workspace must be empty")) {
        return;
    }

    while (wrap->children->length) {
        struct sway_container *child = wrap->children->items[0];
        container_detach(child);
        workspace_add_tiling(ws, child);
    }
}

void workspace_detach(struct sway_workspace *workspace) {
    struct sway_output *output = workspace->output;
    int index = list_find(output->workspaces, workspace);
    if (index != -1) {
        list_del(output->workspaces, index);
    }
    workspace->output = NULL;

    node_set_dirty(&workspace->node);
    node_set_dirty(&output->node);
}

struct sway_container *workspace_add_tiling(struct sway_workspace *workspace,
        struct sway_container *con) {
    if (con->workspace) {
        container_detach(con);
    }
    list_add(workspace->tiling, con);
    con->workspace = workspace;
    container_for_each_child(con, set_workspace, NULL);
    node_set_dirty(&workspace->node);
    node_set_dirty(&con->node);
    return con;
}

void workspace_insert_tiling_direct(struct sway_workspace *workspace,
        struct sway_container *con, int index) {
    list_insert(workspace->tiling, index, con);
    con->workspace = workspace;
    container_for_each_child(con, set_workspace, NULL);
    node_set_dirty(&workspace->node);
    node_set_dirty(&con->node);
}

struct sway_container *workspace_insert_tiling(struct sway_workspace *workspace,
        struct sway_container *con, int index) {
    if (con->workspace) {
        container_detach(con);
    }
    workspace_insert_tiling_direct(workspace, con, index);
    return con;
}

struct sway_container *workspace_split(struct sway_workspace *workspace) {
    if (workspace->tiling->length == 0) {
        return NULL;
    }

    struct sway_container *middle = workspace_wrap_children(workspace);

    struct sway_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        if (seat_get_focus(seat) == &workspace->node) {
            seat_set_focus(seat, &middle->node);
        }
    }

    return middle;
}

void workspace_get_box(struct sway_workspace *workspace, struct wlr_box *box) {
    box->x = workspace->x;
    box->y = workspace->y;
    box->width = workspace->width;
    box->height = workspace->height;
}

static void count_tiling_views(struct sway_container *con, void *data) {
    if (con->view) {
        size_t *count = data;
        *count += 1;
    }
}

size_t workspace_num_tiling_views(struct sway_workspace *ws) {
    size_t count = 0;
    workspace_for_each_container(ws, count_tiling_views, &count);
    return count;
}
