#ifndef _SWAY_NODE_H
#define _SWAY_NODE_H
#include <stdbool.h>
#include "list.h"

#define MIN_SANE_W 100
#define MIN_SANE_H 60

struct sway_output;
struct sway_container;
struct sway_transaction_instruction;
struct wlr_box;

enum wls_transaction_node_type {
    N_OUTPUT,
    N_CONTAINER,
};
struct wls_transaction_node {
    enum wls_transaction_node_type type;
    union {
        struct sway_output *sway_output;
        struct sway_container *sway_container;
    };

    /**
     * A unique ID to identify this node.
     * Primarily used in the get_tree JSON output.
     */
    size_t id;

    struct sway_transaction_instruction *instruction;
    size_t ntxnrefs;
    bool destroying;

    // If true, indicates that the container has pending state that differs from
    // the current.
    bool dirty;

    struct {
        struct wl_signal destroy;
    } events;
};

struct wls_node_manager {
    list_t *transactions;
    list_t *dirty_nodes;
    struct {
        struct wl_signal new_node;
    } events;
};

void node_init(struct wls_transaction_node *node, enum wls_transaction_node_type type, void *thing);

/**
 * Mark a node as dirty if it isn't already. Dirty nodes will be included in the
 * next transaction then unmarked as dirty.
 */
void node_set_dirty(struct wls_transaction_node *node);

bool node_is_view(struct wls_transaction_node *node);

struct sway_output *node_get_output(struct wls_transaction_node *node);

bool node_may_have_container_children(struct wls_transaction_node *node);

struct wls_transaction_node *node_get_parent(struct wls_transaction_node *node);

list_t *node_get_children(struct wls_transaction_node *node);

bool node_has_ancestor(struct wls_transaction_node *node, struct wls_transaction_node *ancestor);

struct wls_node_manager * node_manager_create(void);

void node_manager_destroy(struct wls_node_manager *node_manager);

#endif
