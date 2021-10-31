#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <string.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "config.h"
#include "container.h"
#include "foreach.h"
#include "layers.h"
#include "output.h"
#include "view.h"
#include "wlstem.h"

// =============== HELPERS ===============

struct surface_iterator_data {
    sway_surface_iterator_func_t user_iterator;
    void *user_data;

    struct sway_output *output;
    struct sway_view *view;
    double ox, oy;
    int width, height;
    float rotation;
};

/**
 * Rotate a child's position relative to a parent. The parent size is (pw, ph),
 * the child position is (*sx, *sy) and its size is (sw, sh).
 */
static void rotate_child_position(double *sx, double *sy, double sw, double sh,
        double pw, double ph, float rotation) {
    if (rotation == 0.0f) {
        return;
    }

    // Coordinates relative to the center of the subsurface
    double ox = *sx - pw/2 + sw/2,
        oy = *sy - ph/2 + sh/2;
    // Rotated coordinates
    double rx = cos(-rotation)*ox - sin(-rotation)*oy,
        ry = cos(-rotation)*oy + sin(-rotation)*ox;
    *sx = rx + pw/2 - sw/2;
    *sy = ry + ph/2 - sh/2;
}

static bool get_surface_box(struct surface_iterator_data *data,
        struct wlr_surface *surface, int sx, int sy,
        struct wlr_box *surface_box) {
    struct sway_output *output = data->output;

    if (!wlr_surface_has_buffer(surface)) {
        return false;
    }

    int sw = surface->current.width;
    int sh = surface->current.height;

    double _sx = sx + surface->sx;
    double _sy = sy + surface->sy;
    rotate_child_position(&_sx, &_sy, sw, sh, data->width, data->height,
        data->rotation);

    struct wlr_box box = {
        .x = data->ox + _sx,
        .y = data->oy + _sy,
        .width = sw,
        .height = sh,
    };
    if (surface_box != NULL) {
        memcpy(surface_box, &box, sizeof(struct wlr_box));
    }

    struct wlr_box rotated_box;
    wlr_box_rotated_bounds(&rotated_box, &box, data->rotation);

    struct wlr_box output_box = {
        .width = output->width,
        .height = output->height,
    };

    struct wlr_box intersection;
    return wlr_box_intersection(&intersection, &output_box, &rotated_box);
}

// =============== OUTPUT_LAYOUT ===============

void wls_output_layout_for_each_container(void (*f)(struct sway_container *con, void *data),
        void *data) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        output_for_each_container(output, f, data);
    }

    if (wls->output_manager->noop_output->active) {
        output_for_each_container(wls->output_manager->noop_output, f, data);
    }
}

void wls_output_layout_for_each_output(void (*f)(struct sway_output *output, void *data),
        void *data) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        f(output, data);
    }
}

// =============== OUTPUTS ===============

static void for_each_surface_container_iterator(struct sway_container *con,
        void *_data) {
    if (!con->view || !view_is_visible(con->view)) {
        return;
    }

    struct surface_iterator_data *data = _data;
    output_view_for_each_surface(data->output, con->view,
        data->user_iterator, data->user_data);
}

void output_for_each_surface(struct sway_output *output,
        sway_surface_iterator_func_t iterator, void *user_data) {
    if (output_has_opaque_overlay_layer_surface(output)) {
        goto overlay;
    }

    struct surface_iterator_data data = {
        .user_iterator = iterator,
        .user_data = user_data,
        .output = output,
        .view = NULL,
    };

    if (!output->active) {
        return;
    }

    output_layer_for_each_surface(output,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
        iterator, user_data);
    output_layer_for_each_surface(output,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
        iterator, user_data);

    output_for_each_container(output,
        for_each_surface_container_iterator, &data);

#if HAVE_XWAYLAND
    output_unmanaged_for_each_surface(output, &wls->output_manager->xwayland_unmanaged,
        iterator, user_data);
#endif
    output_layer_for_each_surface(output,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
        iterator, user_data);

overlay:
    output_layer_for_each_surface(output,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
        iterator, user_data);
    output_drag_icons_for_each_surface(output, &wls->output_manager->drag_icons,
        iterator, user_data);
}

static void output_for_each_surface_iterator(struct wlr_surface *surface,
        int sx, int sy, void *_data) {
    struct surface_iterator_data *data = _data;

    struct wlr_box box;
    bool intersects = get_surface_box(data, surface, sx, sy, &box);
    if (!intersects) {
        return;
    }

    data->user_iterator(data->output, data->view, surface, &box, data->rotation,
        data->user_data);
}

void output_surface_for_each_surface(struct sway_output *output,
        struct wlr_surface *surface, double ox, double oy,
        sway_surface_iterator_func_t iterator, void *user_data) {
    struct surface_iterator_data data = {
        .user_iterator = iterator,
        .user_data = user_data,
        .output = output,
        .view = NULL,
        .ox = ox,
        .oy = oy,
        .width = surface->current.width,
        .height = surface->current.height,
        .rotation = 0,
    };

    wlr_surface_for_each_surface(surface,
        output_for_each_surface_iterator, &data);
}

void output_view_for_each_surface(struct sway_output *output,
        struct sway_view *view, sway_surface_iterator_func_t iterator,
        void *user_data) {
    struct surface_iterator_data data = {
        .user_iterator = iterator,
        .user_data = user_data,
        .output = output,
        .view = view,
        .ox = view->container->surface_x - output->lx
            - view->geometry.x,
        .oy = view->container->surface_y - output->ly
            - view->geometry.y,
        .width = view->container->current.content_width,
        .height = view->container->current.content_height,
        .rotation = 0, // TODO
    };

    view_for_each_surface(view, output_for_each_surface_iterator, &data);
}

void output_view_for_each_popup_surface(struct sway_output *output,
        struct sway_view *view, sway_surface_iterator_func_t iterator,
        void *user_data) {
    struct surface_iterator_data data = {
        .user_iterator = iterator,
        .user_data = user_data,
        .output = output,
        .view = view,
        .ox = view->container->surface_x - output->lx
            - view->geometry.x,
        .oy = view->container->surface_y - output->ly
            - view->geometry.y,
        .width = view->container->current.content_width,
        .height = view->container->current.content_height,
        .rotation = 0, // TODO
    };

    view_for_each_popup_surface(view, output_for_each_surface_iterator, &data);
}

void output_layer_for_each_surface(struct sway_output *output,
        struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
        void *user_data) {
    struct sway_layer_surface *layer_surface;
    wl_list_for_each(layer_surface, layer_surfaces, link) {
        struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
            layer_surface->layer_surface;
        output_surface_for_each_surface(output, wlr_layer_surface_v1->surface,
            layer_surface->geo.x, layer_surface->geo.y, iterator,
            user_data);

        struct wlr_xdg_popup *state;
        wl_list_for_each(state, &wlr_layer_surface_v1->popups, link) {
            struct wlr_xdg_surface *popup = state->base;
            if (!popup->configured) {
                continue;
            }

            double popup_sx, popup_sy;
            popup_sx = layer_surface->geo.x +
                popup->popup->geometry.x - popup->geometry.x;
            popup_sy = layer_surface->geo.y +
                popup->popup->geometry.y - popup->geometry.y;

            struct wlr_surface *surface = popup->surface;

            struct surface_iterator_data data = {
                .user_iterator = iterator,
                .user_data = user_data,
                .output = output,
                .view = NULL,
                .ox = popup_sx,
                .oy = popup_sy,
                .width = surface->current.width,
                .height = surface->current.height,
                .rotation = 0,
            };

            wlr_xdg_surface_for_each_surface(
                    popup, output_for_each_surface_iterator, &data);
        }
    }
}

void output_layer_for_each_toplevel_surface(struct sway_output *output,
        struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
        void *user_data) {
    struct sway_layer_surface *layer_surface;
    wl_list_for_each(layer_surface, layer_surfaces, link) {
        struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
            layer_surface->layer_surface;
        output_surface_for_each_surface(output, wlr_layer_surface_v1->surface,
            layer_surface->geo.x, layer_surface->geo.y, iterator,
            user_data);
    }
}


void output_layer_for_each_popup_surface(struct sway_output *output,
        struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
        void *user_data) {
    struct sway_layer_surface *layer_surface;
    wl_list_for_each(layer_surface, layer_surfaces, link) {
        struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
            layer_surface->layer_surface;

        struct wlr_xdg_popup *state;
        wl_list_for_each(state, &wlr_layer_surface_v1->popups, link) {
            struct wlr_xdg_surface *popup = state->base;
            if (!popup->configured) {
                continue;
            }

            double popup_sx, popup_sy;
            popup_sx = layer_surface->geo.x +
                popup->popup->geometry.x - popup->geometry.x;
            popup_sy = layer_surface->geo.y +
                popup->popup->geometry.y - popup->geometry.y;

            struct wlr_surface *surface = popup->surface;

            struct surface_iterator_data data = {
                .user_iterator = iterator,
                .user_data = user_data,
                .output = output,
                .view = NULL,
                .ox = popup_sx,
                .oy = popup_sy,
                .width = surface->current.width,
                .height = surface->current.height,
                .rotation = 0,
            };

            wlr_xdg_surface_for_each_surface(
                    popup, output_for_each_surface_iterator, &data);
        }
    }
}

#if HAVE_XWAYLAND
void output_unmanaged_for_each_surface(struct sway_output *output,
        struct wl_list *unmanaged, sway_surface_iterator_func_t iterator,
        void *user_data) {
    struct sway_xwayland_unmanaged *unmanaged_surface;
    wl_list_for_each(unmanaged_surface, unmanaged, link) {
        struct wlr_xwayland_surface *xsurface =
            unmanaged_surface->wlr_xwayland_surface;
        double ox = unmanaged_surface->lx - output->lx;
        double oy = unmanaged_surface->ly - output->ly;

        output_surface_for_each_surface(output, xsurface->surface, ox, oy,
            iterator, user_data);
    }
}
#endif

void output_drag_icons_for_each_surface(struct sway_output *output,
        struct wl_list *drag_icons, sway_surface_iterator_func_t iterator,
        void *user_data) {
    struct sway_drag_icon *drag_icon;
    wl_list_for_each(drag_icon, drag_icons, link) {
        double ox = drag_icon->x - output->lx;
        double oy = drag_icon->y - output->ly;

        if (drag_icon->wlr_drag_icon->mapped) {
            output_surface_for_each_surface(output,
                drag_icon->wlr_drag_icon->surface, ox, oy,
                iterator, user_data);
        }
    }
}

void output_for_each_container(struct sway_output *output,
        void (*f)(struct sway_container *con, void *data), void *data) {
    if (output->active) {
        for (int i = 0; i < output->windows->length; ++i) {
            struct sway_container *container = output->windows->items[i];
            f(container, data);
        }
    }
}

// =============== VIEWS ===============

void view_for_each_surface(struct sway_view *view,
        wlr_surface_iterator_func_t iterator, void *user_data) {
    if (!view->surface) {
        return;
    }
    if (view->impl->for_each_surface) {
        view->impl->for_each_surface(view, iterator, user_data);
    } else {
        wlr_surface_for_each_surface(view->surface, iterator, user_data);
    }
}

void view_for_each_popup_surface(struct sway_view *view,
        wlr_surface_iterator_func_t iterator, void *user_data) {
    if (!view->surface) {
        return;
    }
    if (view->impl->for_each_popup_surface) {
        view->impl->for_each_popup_surface(view, iterator, user_data);
    }
}
