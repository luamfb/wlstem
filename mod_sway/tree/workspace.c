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

struct sway_workspace *workspace_create(struct sway_output *output) {
    if (!sway_assert(output != NULL, "Tried to create workspace for NULL output")) {
        abort();
    }
    if (!sway_assert(!output->active_workspace,
            "Tried to create workspace to output which already has one")) {
        abort();
    }

    sway_log(SWAY_DEBUG, "Adding workspace for output %s",
            output->wlr_output->name);

    struct sway_workspace *ws = calloc(1, sizeof(struct sway_workspace));
    if (!ws) {
        sway_log(SWAY_ERROR, "Unable to allocate sway_workspace");
        return NULL;
    }
    node_init(&ws->node, N_WORKSPACE, ws);

    output->active_workspace = ws;
    ws->output = output;
    node_set_dirty(&output->node);
    node_set_dirty(&ws->node);

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

    free(workspace);
}

void workspace_begin_destroy(struct sway_workspace *workspace) {
    sway_log(SWAY_DEBUG, "Destroying workspace %p", workspace);
    wl_signal_emit(&workspace->node.events.destroy, &workspace->node);

    if (workspace->output) {
        workspace_detach(workspace);
    }
    workspace->node.destroying = true;
    node_set_dirty(&workspace->node);
}

bool workspace_is_visible(struct sway_workspace *ws) {
    if (ws->node.destroying) {
        return false;
    }
    return output_get_active_workspace(ws->output) == ws;
}

bool workspace_is_empty(struct sway_workspace *ws) {
    if (!ws->output) {
        sway_log(SWAY_DEBUG, "workspace has no output!");
        return false;
    }
    if (ws->output->tiling->length) {
        return false;
    }
    return true;
}

void workspace_for_each_container(struct sway_workspace *ws,
        void (*f)(struct sway_container *con, void *data), void *data) {
    struct sway_output *output = ws->output;
    if (!output) {
        sway_log(SWAY_DEBUG, "workspace has no output!");
        return;
    }
    for (int i = 0; i < ws->output->tiling->length; ++i) {
        struct sway_container *container = ws->output->tiling->items[i];
        f(container, data);
    }
}

void workspace_detach(struct sway_workspace *workspace) {
    struct sway_output *output = workspace->output;
    if (output->active_workspace == workspace) {
        output->active_workspace = NULL;
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
    list_add(workspace->output->tiling, con);
    con->workspace = workspace;
    node_set_dirty(&workspace->node);
    node_set_dirty(&con->node);
    return con;
}
