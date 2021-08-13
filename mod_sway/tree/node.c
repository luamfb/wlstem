#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "sway/output.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/root.h"
#include "list.h"
#include "log.h"
#include "wlstem.h"

struct wls_node_manager * node_manager_create(void) {
    struct wls_node_manager *node_manager =
        calloc(1, sizeof(struct wls_node_manager));
    if (!node_manager) {
        sway_log(SWAY_ERROR, "node manager creation failed");
        return NULL;
    }
    node_manager->dirty_nodes = create_list();
    return node_manager;
}

void node_manager_destroy(struct wls_node_manager *node_manager) {
    if (!node_manager) {
        return;
    }
    list_free(node_manager->dirty_nodes);
    free(node_manager);
}

void node_init(struct sway_node *node, enum sway_node_type type, void *thing) {
    static size_t next_id = 1;
    if (type != N_OUTPUT && type != N_CONTAINER) {
        sway_log(SWAY_ERROR, "node_init: invalid node type %d (%x)", type, type);
    }
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
    list_add(wls->node_manager->dirty_nodes, node);
}

bool node_is_view(struct sway_node *node) {
    return node->type == N_CONTAINER && node->sway_container->view;
}

struct sway_output *node_get_output(struct sway_node *node) {
    switch (node->type) {
    case N_CONTAINER:
        return node->sway_container->output;
    case N_OUTPUT:
        return node->sway_output;
    }
    return NULL;
}

bool node_may_have_container_children(struct sway_node *node) {
    switch (node->type) {
    case N_CONTAINER:
    case N_OUTPUT:
        return true;
    }
    return false;
}

struct sway_node *node_get_parent(struct sway_node *node) {
    switch (node->type) {
    case N_CONTAINER:
        if (node->sway_container->output) {
            return &node->sway_container->output->node;
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
    case N_OUTPUT:
        return node->sway_output->tiling;
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
