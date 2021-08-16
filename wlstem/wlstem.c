#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <wayland-server-core.h>
#include "log.h"
#include "node.h"
#include "root.h"
#include "wlstem.h"

struct wls_context *wls = NULL;

bool wls_init(void) {
    if (wls) {
        sway_log(SWAY_ERROR, "the wlstem context was already initialized!");
        return false;
    }

    struct wls_context *_wls = calloc(1, sizeof(struct wls_context));
    struct wls_node_manager *_node_manager =
        node_manager_create();
    struct sway_root *_root = root_create();

    if (!_wls || !_node_manager || !_root) {
        sway_log(SWAY_ERROR, "wlstem initialization failed!");
        return false;
    }

    wls = _wls;
    wls->node_manager = _node_manager;
    wls->root = _root;

    return true;
}

void wls_fini(void) {
    if (!wls) {
        return; // nothing to do.
    }
    node_manager_destroy(wls->node_manager);
    root_destroy(wls->root);
    free(wls);
    wls = NULL;
}
