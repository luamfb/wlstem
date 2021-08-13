#ifndef WLSTEM_WLSTEM_H
#define WLSTEM_WLSTEM_H

#include <stdbool.h>
#include "sway/tree/node.h"

struct wls_context {
    struct wls_node_manager *node_manager;
};

// The context for wlstem.
// This is a NULL pointer prior to calling `wls_init`
// (and also after calling `wls_fini`).
extern struct wls_context *wls;

// Initializes the `wls` variable containing wlstem's context.
// Returns whether or not the initialization succeeded.
//
bool wls_init(void);

// Finalizes the wlstem context, freeing up any resources it used so far.
void wls_fini(void);

#endif
