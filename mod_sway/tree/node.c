#define _POSIX_C_SOURCE 200809L
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "log.h"

void node_init(struct sway_node *node, enum sway_node_type type, void *thing) {
    static size_t next_id = 1;
    node->id = next_id++;
    node->type = type;
    node->sway_output = thing;
    wl_signal_init(&node->events.destroy);
}

void node_set_dirty(struct sway_node *node) {
    if (node->dirty) {
        return;
    }
    node->dirty = true;
    list_add(server.dirty_nodes, node);
}

bool node_is_view(struct sway_node *node) {
    return node->type == N_CONTAINER && node->sway_container->view;
}

struct sway_output *node_get_output(struct sway_node *node) {
    switch (node->type) {
    case N_CONTAINER:
        return node->sway_container->output;
    case N_WORKSPACE:
        return node->sway_workspace->output;
    case N_OUTPUT:
        return node->sway_output;
    }
    return NULL;
}

bool node_may_have_container_children(struct sway_node *node) {
    switch (node->type) {
    case N_CONTAINER:
    case N_WORKSPACE:
        return true;
    case N_OUTPUT:
        return false;
    }
    return false;
}

struct sway_node *node_get_parent(struct sway_node *node) {
    switch (node->type) {
    case N_CONTAINER:
        if (node->sway_container->output) {
            struct sway_workspace *ws = node->sway_container->output->active_workspace;
            if (ws) {
                return &ws->node;
            }
        }
        return NULL;
    case N_WORKSPACE: {
            struct sway_workspace *ws = node->sway_workspace;
            if (ws->output) {
                return &ws->output->node;
            }
        }
        return NULL;
    case N_OUTPUT:
        // To differentiate from the NULL cases, an output's parent
        // is itself.
        return node;
    }
    return NULL;
}

list_t *node_get_children(struct sway_node *node) {
    switch (node->type) {
    case N_WORKSPACE:
        {
            struct sway_output *output = node->sway_workspace->output;
            if (output) {
                return output->tiling;
            }
            sway_log(SWAY_DEBUG, "workspace has no output!");
            return NULL;
        }
    case N_OUTPUT:
    case N_CONTAINER:
        return NULL;
    }
    return NULL;
}

bool node_has_ancestor(struct sway_node *node, struct sway_node *ancestor) {
    struct sway_node *prev = node;
    struct sway_node *parent = node_get_parent(node);
    while (parent && prev != parent) {
        if (parent == ancestor) {
            return true;
        }
        prev = parent;
        parent = node_get_parent(parent);
    }
    return false;
}
