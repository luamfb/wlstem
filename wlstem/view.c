#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "container.h"
#include "log.h"
#include "sway_view.h"

bool view_is_visible(struct sway_view *view) {
    if (view->container->node.destroying) {
        return false;
    }
    struct sway_output *output = view->container->output;
    if (!output) {
        return false;
    }
    return true;
}

struct sway_view *view_from_wlr_xdg_surface(
        struct wlr_xdg_surface *xdg_surface) {
    return xdg_surface->data;
}

#if HAVE_XWAYLAND
struct sway_view *view_from_wlr_xwayland_surface(
        struct wlr_xwayland_surface *xsurface) {
    return xsurface->data;
}
#endif

struct sway_view *view_from_wlr_surface(struct wlr_surface *wlr_surface) {
    if (wlr_surface_is_xdg_surface(wlr_surface)) {
        struct wlr_xdg_surface *xdg_surface =
            wlr_xdg_surface_from_wlr_surface(wlr_surface);
        return view_from_wlr_xdg_surface(xdg_surface);
    }
#if HAVE_XWAYLAND
    if (wlr_surface_is_xwayland_surface(wlr_surface)) {
        struct wlr_xwayland_surface *xsurface =
            wlr_xwayland_surface_from_wlr_surface(wlr_surface);
        return view_from_wlr_xwayland_surface(xsurface);
    }
#endif
    if (wlr_surface_is_subsurface(wlr_surface)) {
        struct wlr_subsurface *subsurface =
            wlr_subsurface_from_wlr_surface(wlr_surface);
        return view_from_wlr_surface(subsurface->parent);
    }
    if (wlr_surface_is_layer_surface(wlr_surface)) {
        return NULL;
    }

    const char *role = wlr_surface->role ? wlr_surface->role->name : NULL;
    sway_log(SWAY_DEBUG, "Surface of unknown type (role %s): %p",
        role, wlr_surface);
    return NULL;
}
