#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <wayland-server-core.h>
#include "log.h"
#include "node.h"
#include "root.h"
#include "wls_server.h"
#include "wlstem.h"

struct wls_context *wls = NULL;

bool wls_init(void) {
    if (wls) {
        sway_log(SWAY_ERROR, "the wlstem context was already initialized!");
        return false;
    }

    struct wls_context *_wls = calloc(1, sizeof(struct wls_context));
    struct wls_server *_server = wls_server_create();

    struct wls_node_manager *_node_manager =
        node_manager_create();
    struct sway_root *_root = root_create();

    if (!_wls || !_server || !_node_manager || !_root) {
        sway_log(SWAY_ERROR, "wlstem initialization failed!");
        return false;
    }

    wls = _wls;
    wls->server = _server;
    wls->node_manager = _node_manager;
    wls->root = _root;

    return true;
}

void wls_fini(void) {
    if (!wls) {
        return; // nothing to do.
    }
    // This needs uses root node and the the dirty_nodes list,
    // so call it before destroying them
    wls_server_destroy(wls->server);

    root_destroy(wls->root);
    node_manager_destroy(wls->node_manager);
    free(wls);
    wls = NULL;
}
