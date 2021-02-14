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

static bool workspace_valid_on_output(const char *output_name,
        const char *ws_name) {
    struct sway_output *output = output_by_name_or_id(output_name);
    if (!output) {
        return false;
    }
    return true;
}

static void workspace_name_from_binding(const struct sway_binding * binding,
        const char* output_name, int *min_order, char **earliest_name) {
    char *name = NULL;
    // workspace n
    char *cmd = "workspace number 1";

    // TODO: support "move container to workspace" bindings as well

    if (strcmp("workspace", cmd) == 0 && name) {
        char *_target = strdup(name);
        strip_quotes(_target);
        sway_log(SWAY_DEBUG, "Got valid workspace command for target: '%s'",
                _target);

        // Make sure that the command references an actual workspace
        // not a command about workspaces
        if (strcmp(_target, "next") == 0 ||
                strcmp(_target, "prev") == 0 ||
                strncmp(_target, "next_on_output",
                    strlen("next_on_output")) == 0 ||
                strncmp(_target, "prev_on_output",
                    strlen("next_on_output")) == 0 ||
                strcmp(_target, "number") == 0 ||
                strcmp(_target, "current") == 0) {
            free(_target);
            return;
        }

        // If the command is workspace number <name>, isolate the name
        if (strncmp(_target, "number ", strlen("number ")) == 0) {
            size_t length = strlen(_target) - strlen("number ") + 1;
            char *temp = malloc(length);
            strncpy(temp, _target + strlen("number "), length - 1);
            temp[length - 1] = '\0';
            free(_target);
            _target = temp;
            sway_log(SWAY_DEBUG, "Isolated name from workspace number: '%s'", _target);

            // Make sure the workspace number doesn't already exist
            if (isdigit(_target[0]) && workspace_by_number(_target)) {
                free(_target);
                return;
            }
        }

        // Make sure that the workspace doesn't already exist
        if (workspace_by_name(_target)) {
            free(_target);
            return;
        }

        // make sure that the workspace can appear on the given
        // output
        if (!workspace_valid_on_output(output_name, _target)) {
            free(_target);
            return;
        }

        if (binding->order < *min_order) {
            *min_order = binding->order;
            free(*earliest_name);
            *earliest_name = _target;
            sway_log(SWAY_DEBUG, "Workspace: Found free name %s", _target);
        } else {
            free(_target);
        }
    }
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

static bool _workspace_by_name(struct sway_workspace *ws, void *data) {
    return strcasecmp(ws->name, data) == 0;
}

struct sway_workspace *workspace_by_name(const char *name) {
    struct sway_seat *seat = input_manager_current_seat();
    struct sway_workspace *current = seat_get_focused_workspace(seat);

    if (current && strcmp(name, "prev") == 0) {
        return workspace_prev(current);
    } else if (current && strcmp(name, "prev_on_output") == 0) {
        return workspace_output_prev(current, false);
    } else if (current && strcmp(name, "next") == 0) {
        return workspace_next(current);
    } else if (current && strcmp(name, "next_on_output") == 0) {
        return workspace_output_next(current, false);
    } else if (strcmp(name, "current") == 0) {
        return current;
    } else {
        return root_find_workspace(_workspace_by_name, (void*)name);
    }
}

static int workspace_get_number(struct sway_workspace *workspace) {
    char *endptr = NULL;
    errno = 0;
    long long n = strtoll(workspace->name, &endptr, 10);
    if (errno != 0 || n > INT32_MAX || n < 0 || endptr == workspace->name) {
        n = -1;
    }
    return n;
}

struct sway_workspace *workspace_prev(struct sway_workspace *workspace) {
    int n = workspace_get_number(workspace);
    struct sway_workspace *prev = NULL, *last = NULL, *other = NULL;
    bool found = false;
    if (n < 0) {
        // Find the prev named workspace
        int othern = -1;
        for (int i = root->outputs->length - 1; i >= 0; i--) {
            struct sway_output *output = root->outputs->items[i];
            for (int j = output->workspaces->length - 1; j >= 0; j--) {
                struct sway_workspace *ws = output->workspaces->items[j];
                int wsn = workspace_get_number(ws);
                if (!last) {
                    // The first workspace in reverse order
                    last = ws;
                }
                if (!other || (wsn >= 0 && wsn > othern)) {
                    // The last (greatest) numbered workspace.
                    other = ws;
                    othern = workspace_get_number(other);
                }
                if (ws == workspace) {
                    found = true;
                } else if (wsn < 0 && found) {
                    // Found a non-numbered workspace before current
                    return ws;
                }
            }
        }
    } else {
        // Find the prev numbered workspace
        int prevn = -1, lastn = -1;
        for (int i = root->outputs->length - 1; i >= 0; i--) {
            struct sway_output *output = root->outputs->items[i];
            for (int j = output->workspaces->length - 1; j >= 0; j--) {
                struct sway_workspace *ws = output->workspaces->items[j];
                int wsn = workspace_get_number(ws);
                if (!last || (wsn >= 0 && wsn > lastn)) {
                    // The greatest numbered (or last) workspace
                    last = ws;
                    lastn = workspace_get_number(last);
                }
                if (!other && wsn < 0) {
                    // The last named workspace
                    other = ws;
                }
                if (wsn < 0) {
                    // Haven't reached the numbered workspaces
                    continue;
                }
                if (wsn < n && (!prev || wsn > prevn)) {
                    // The closest workspace before the current
                    prev = ws;
                    prevn = workspace_get_number(prev);
                }
            }
        }
    }

    if (!prev) {
        prev = other ? other : last;
    }
    return prev;
}

struct sway_workspace *workspace_next(struct sway_workspace *workspace) {
    int n = workspace_get_number(workspace);
    struct sway_workspace *next = NULL, *first = NULL, *other = NULL;
    bool found = false;
    if (n < 0) {
        // Find the next named workspace
        int othern = -1;
        for (int i = 0; i < root->outputs->length; i++) {
            struct sway_output *output = root->outputs->items[i];
            for (int j = 0; j < output->workspaces->length; j++) {
                struct sway_workspace *ws = output->workspaces->items[j];
                int wsn = workspace_get_number(ws);
                if (!first) {
                    // The first named workspace
                    first = ws;
                }
                if (!other || (wsn >= 0 && wsn < othern)) {
                    // The first (least) numbered workspace
                    other = ws;
                    othern = workspace_get_number(other);
                }
                if (ws == workspace) {
                    found = true;
                } else if (wsn < 0 && found) {
                    // The first non-numbered workspace after the current
                    return ws;
                }
            }
        }
    } else {
        // Find the next numbered workspace
        int nextn = -1, firstn = -1;
        for (int i = 0; i < root->outputs->length; i++) {
            struct sway_output *output = root->outputs->items[i];
            for (int j = 0; j < output->workspaces->length; j++) {
                struct sway_workspace *ws = output->workspaces->items[j];
                int wsn = workspace_get_number(ws);
                if (!first || (wsn >= 0 && wsn < firstn)) {
                    // The first (or least numbered) workspace
                    first = ws;
                    firstn = workspace_get_number(first);
                }
                if (!other && wsn < 0) {
                    // The first non-numbered workspace
                    other = ws;
                }
                if (wsn < 0) {
                    // Checked all the numbered workspaces
                    break;
                }
                if (n < wsn && (!next || wsn < nextn)) {
                    // The first workspace numerically after the current
                    next = ws;
                    nextn = workspace_get_number(next);
                }
            }
        }
    }

    if (!next) {
        // If there is no next workspace from the same category, return the
        // first from this category.
        next = other ? other : first;
    }
    return next;
}

/**
 * Get the previous or next workspace on the specified output. Wraps around at
 * the end and beginning.  If next is false, the previous workspace is returned,
 * otherwise the next one is returned.
 */
static struct sway_workspace *workspace_output_prev_next_impl(
        struct sway_output *output, int dir, bool create) {
    struct sway_seat *seat = input_manager_current_seat();
    struct sway_workspace *workspace = seat_get_focused_workspace(seat);
    if (!workspace) {
        sway_log(SWAY_DEBUG,
                "No focused workspace to base prev/next on output off of");
        return NULL;
    }

    int index = list_find(output->workspaces, workspace);
    if (!workspace_is_empty(workspace) && create &&
            (index + dir < 0 || index + dir == output->workspaces->length)) {
        struct sway_output *output = workspace->output;
        char *next = workspace_next_name(output->wlr_output->name);
        workspace_create(output, next);
        free(next);
    }
    size_t new_index = wrap(index + dir, output->workspaces->length);
    return output->workspaces->items[new_index];
}

struct sway_workspace *workspace_output_next(
        struct sway_workspace *current, bool create) {
    return workspace_output_prev_next_impl(current->output, 1, create);
}

struct sway_workspace *workspace_output_prev(
        struct sway_workspace *current, bool create) {
    return workspace_output_prev_next_impl(current->output, -1, create);
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

void workspace_output_raise_priority(struct sway_workspace *ws,
        struct sway_output *old_output, struct sway_output *output) {
    int old_index = workspace_output_get_priority(ws, old_output);
    if (old_index < 0) {
        return;
    }

    int new_index = workspace_output_get_priority(ws, output);
    if (new_index < 0) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        list_insert(ws->output_priority, old_index, strdup(identifier));
    } else if (new_index > old_index) {
        char *name = ws->output_priority->items[new_index];
        list_del(ws->output_priority, new_index);
        list_insert(ws->output_priority, old_index, name);
    }
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
    container_handle_fullscreen_reparent(con);
    node_set_dirty(&ws->node);
    node_set_dirty(&con->node);
}

struct sway_container *workspace_wrap_children(struct sway_workspace *ws) {
    struct sway_container *fs = ws->fullscreen;
    struct sway_container *middle = container_create(NULL);
    while (ws->tiling->length) {
        struct sway_container *child = ws->tiling->items[0];
        container_detach(child);
        container_add_child(middle, child);
    }
    workspace_attach_tiling(ws, middle);
    ws->fullscreen = fs;
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
    container_handle_fullscreen_reparent(con);
    node_set_dirty(&workspace->node);
    node_set_dirty(&con->node);
    return con;
}

void workspace_insert_tiling_direct(struct sway_workspace *workspace,
        struct sway_container *con, int index) {
    list_insert(workspace->tiling, index, con);
    con->workspace = workspace;
    container_for_each_child(con, set_workspace, NULL);
    container_handle_fullscreen_reparent(con);
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
