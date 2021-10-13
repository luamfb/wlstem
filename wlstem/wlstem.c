#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <wayland-server-core.h>
#include "input_method.h"
#include "list.h"
#include "log.h"
#include "node.h"
#include "output_config.h"
#include "output_manager.h"
#include "server.h"
#include "wlstem.h"

struct wls_context *wls = NULL;

bool wls_init(void) {
    if (wls) {
        sway_log(SWAY_ERROR, "the wlstem context was already initialized!");
        return false;
    }

    struct wls_context *_wls = calloc(1, sizeof(struct wls_context));
    struct wls_server *_server = wls_server_create();

    if (!_wls || !_server) {
        sway_log(SWAY_ERROR, "wlstem initialization (1st stage) failed!");
        return false;
    }

    struct wls_node_manager *_node_manager =
        node_manager_create();

    struct wls_output_manager *_output_manager = wls_output_manager_create(_server);

    list_t *_output_configs = create_list();

    struct wls_input_method_manager *_input_method_manager =
        wls_input_method_manager_create(_server->wl_display);

    struct wlr_tablet_manager_v2 *_tablet_v2 = wlr_tablet_v2_create(
        _server->wl_display);

    if (!_node_manager || !_output_manager || !_output_configs
            || !_input_method_manager || !_tablet_v2) {
        sway_log(SWAY_ERROR, "wlstem initialization (2nd stage) failed!");
        return false;
    }

    wls = _wls;
    wls->server = _server;
    wls->node_manager = _node_manager;
    wls->output_manager = _output_manager;
    wls->output_configs = _output_configs;
    wls->input_method_manager = _input_method_manager;
    wls->tablet_v2 = _tablet_v2;

    return true;
}

void wls_fini(void) {
    if (!wls) {
        return; // nothing to do.
    }
    // This needs the output_manager and the the dirty_nodes list,
    // so call it before destroying them
    wls_server_destroy(wls->server);

    wls_output_manager_destroy(wls->output_manager);
    node_manager_destroy(wls->node_manager);

    if (wls->output_configs) {
        for (int i = 0; i < wls->output_configs->length; i++) {
            free_output_config(wls->output_configs->items[i]);
        }
        list_free(wls->output_configs);
    }

    free(wls);
    wls = NULL;
}
