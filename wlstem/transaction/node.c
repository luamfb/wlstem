#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "output.h"
#include "node.h"
#include "output_manager.h"
#include "list.h"
#include "log.h"
#include "window.h"
#include "wlstem.h"

struct wls_node_manager * node_manager_create(void) {
    struct wls_node_manager *node_manager =
        calloc(1, sizeof(struct wls_node_manager));
    if (!node_manager) {
        sway_log(SWAY_ERROR, "node manager creation failed");
        return NULL;
    }
    node_manager->transactions = create_list();
    node_manager->dirty_nodes = create_list();

    wl_signal_init(&node_manager->events.new_node);
    return node_manager;
}

void node_manager_destroy(struct wls_node_manager *node_manager) {
    if (!node_manager) {
        return;
    }
    list_free(node_manager->transactions);
    list_free(node_manager->dirty_nodes);
    free(node_manager);
}

void node_init(struct wls_transaction_node *node, enum wls_transaction_node_type type, void *thing) {
    static size_t next_id = 1;
    if (type != N_OUTPUT && type != N_WINDOW) {
        sway_log(SWAY_ERROR, "node_init: invalid node type %d (%x)", type, type);
    }
    node->id = next_id++;
    node->type = type;
    node->sway_output = thing;
    wl_signal_init(&node->events.destroy);
}

void node_set_dirty(struct wls_transaction_node *node) {
    if (node->dirty) {
        return;
    }
    node->dirty = true;
    list_add(wls->node_manager->dirty_nodes, node);
}

bool node_is_view(struct wls_transaction_node *node) {
    return node->type == N_WINDOW && node->wls_window->view;
}

struct sway_output *node_get_output(struct wls_transaction_node *node) {
    switch (node->type) {
    case N_WINDOW:
        return node->wls_window->output;
    case N_OUTPUT:
        return node->sway_output;
    }
    return NULL;
}

bool node_may_have_window_children(struct wls_transaction_node *node) {
    switch (node->type) {
    case N_WINDOW:
    case N_OUTPUT:
        return true;
    }
    return false;
}

struct wls_transaction_node *node_get_parent(struct wls_transaction_node *node) {
    switch (node->type) {
    case N_WINDOW:
        if (node->wls_window->output) {
            return &node->wls_window->output->node;
        }
        return NULL;
    case N_OUTPUT:
        // To differentiate from the NULL cases, an output's parent
        // is itself.
        return node;
    }
    return NULL;
}

list_t *node_get_children(struct wls_transaction_node *node) {
    switch (node->type) {
    case N_OUTPUT:
        return node->sway_output->windows;
    case N_WINDOW:
        return NULL;
    }
    return NULL;
}

bool node_has_ancestor(struct wls_transaction_node *node, struct wls_transaction_node *ancestor) {
    struct wls_transaction_node *prev = node;
    struct wls_transaction_node *parent = node_get_parent(node);
    while (parent && prev != parent) {
        if (parent == ancestor) {
            return true;
        }
        prev = parent;
        parent = node_get_parent(parent);
    }
    return false;
}
