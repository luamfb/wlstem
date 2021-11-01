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

struct sway_container *view_container_at(struct wls_transaction_node *parent,
        double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    if (!sway_assert(node_is_view(parent), "Expected a view")) {
        return NULL;
    }

    struct sway_container *container = parent->sway_container;
    struct wlr_box box = {
            .x = container->x,
            .y = container->y,
            .width = container->width,
            .height = container->height,
    };

    if (wlr_box_contains_point(&box, lx, ly)) {
        surface_at_view(parent->sway_container, lx, ly, surface, sx, sy);
        return container;
    }

    return NULL;
}

static struct sway_container *toplevel_window_at_recurse(struct wls_transaction_node *parent,
        double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    list_t *children = node_get_children(parent);
    if (children) {
        for (int i = 0; i < children->length; ++i) {
            struct sway_container *child = children->items[i];
            struct sway_container *container =
                toplevel_window_at(&child->node, lx, ly, surface, sx, sy);
            if (container) {
                return container;
            }
        }
    }
    return NULL;
}

struct sway_container *toplevel_window_at(struct wls_transaction_node *parent,
        double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    if (node_is_view(parent)) {
        return view_container_at(parent, lx, ly, surface, sx, sy);
    }
    if (!node_get_children(parent)) {
        return NULL;
    }
    if (node_may_have_container_children(parent)) {
        return toplevel_window_at_recurse(parent, lx, ly, surface, sx, sy);
    }
    return NULL;
}
