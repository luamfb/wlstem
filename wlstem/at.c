#define _POSIX_C_SOURCE 200809L
#include <wayland-server-core.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "config.h"
#include "container.h"
#include "log.h"
#include "view.h"

struct sway_container *surface_at_view(struct sway_container *con, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    if (!sway_assert(con->view, "Expected a view")) {
        return NULL;
    }
    struct sway_view *view = con->view;
    double view_sx = lx - con->surface_x + view->geometry.x;
    double view_sy = ly - con->surface_y + view->geometry.y;

    double _sx, _sy;
    struct wlr_surface *_surface = NULL;
    switch (view->type) {
#if HAVE_XWAYLAND
    case SWAY_VIEW_XWAYLAND:
        _surface = wlr_surface_surface_at(view->surface,
                view_sx, view_sy, &_sx, &_sy);
        break;
#endif
    case SWAY_VIEW_XDG_SHELL:
        _surface = wlr_xdg_surface_surface_at(
                view->wlr_xdg_surface,
                view_sx, view_sy, &_sx, &_sy);
        break;
    }
    if (_surface) {
        *sx = _sx;
        *sy = _sy;
        *surface = _surface;
        return con;
    }
    return NULL;
}
