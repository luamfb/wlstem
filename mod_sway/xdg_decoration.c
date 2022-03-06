#include <stdlib.h>
#include "sway_transaction.h"
#include "sway_server.h"
#include "sway_arrange.h"
#include "sway_view.h"
#include "sway_xdg_decoration.h"
#include "log.h"

static void xdg_decoration_handle_destroy(struct wl_listener *listener,
        void *data) {
    struct sway_xdg_decoration *deco =
        wl_container_of(listener, deco, destroy);
    if(deco->view) {
        deco->view->xdg_decoration = NULL;
    }
    wl_list_remove(&deco->destroy.link);
    wl_list_remove(&deco->request_mode.link);
    wl_list_remove(&deco->link);
    free(deco);
}

static void xdg_decoration_handle_request_mode(struct wl_listener *listener,
        void *data) {
    struct sway_xdg_decoration *deco =
        wl_container_of(listener, deco, request_mode);
    wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void handle_xdg_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
    struct sway_xdg_shell_view *xdg_shell_view = wlr_deco->surface->data;

    struct sway_xdg_decoration *deco = calloc(1, sizeof(*deco));
    if (deco == NULL) {
        return;
    }

    deco->view = &xdg_shell_view->view;
    deco->view->xdg_decoration = deco;
    deco->wlr_xdg_decoration = wlr_deco;

    wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
    deco->destroy.notify = xdg_decoration_handle_destroy;

    wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);
    deco->request_mode.notify = xdg_decoration_handle_request_mode;

    wl_list_insert(&server.xdg_decorations, &deco->link);

    xdg_decoration_handle_request_mode(&deco->request_mode, wlr_deco);
}

struct sway_xdg_decoration *xdg_decoration_from_surface(
        struct wlr_surface *surface) {
    struct sway_xdg_decoration *deco;
    wl_list_for_each(deco, &server.xdg_decorations, link) {
        if (deco->wlr_xdg_decoration->surface->surface == surface) {
            return deco;
        }
    }
    return NULL;
}
