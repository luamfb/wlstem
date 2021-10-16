#ifndef WLSTEM_WLSTEM_H
#define WLSTEM_WLSTEM_H

#include <stdbool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include "list.h"
#include "node.h"
#include "misc_protocols.h"
#include "output_manager.h"

struct wls_context {
    struct wls_server *server;
    struct wls_node_manager *node_manager;
    struct wls_output_manager *output_manager;
    list_t *output_configs;
    struct wls_input_method_manager *input_method_manager;
    struct wlr_tablet_manager_v2 *tablet_v2;
    struct wl_list seats;
    struct wls_misc_protocols *misc_protocols;
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
