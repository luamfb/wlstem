#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "foreach.h"
#include "log.h"
#include "view.h"
#include "window.h"

void view_destroy(struct sway_view *view) {
    if (!sway_assert(view->surface == NULL, "Tried to free mapped view")) {
        return;
    }
    if (!sway_assert(view->destroying,
                "Tried to free view which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(view->window == NULL,
                "Tried to free view which still has a window "
                "(might have a pending transaction?)")) {
        return;
    }
    if (!wl_list_empty(&view->saved_buffers)) {
        view_remove_saved_buffer(view);
    }

    free(view->title_format);

    if (view->impl->destroy) {
        view->impl->destroy(view);
    } else {
        free(view);
    }
}

static void view_save_buffer_iterator(struct wlr_surface *surface,
        int sx, int sy, void *data) {
    struct sway_view *view = data;

    if (surface && wlr_surface_has_buffer(surface)) {
        wlr_buffer_lock(&surface->buffer->base);
        struct sway_saved_buffer *saved_buffer = calloc(1, sizeof(struct sway_saved_buffer));
        saved_buffer->buffer = surface->buffer;
        saved_buffer->width = surface->current.width;
        saved_buffer->height = surface->current.height;
        saved_buffer->x = sx;
        saved_buffer->y = sy;
        saved_buffer->transform = surface->current.transform;
        wlr_surface_get_buffer_source_box(surface, &saved_buffer->source_box);
        wl_list_insert(&view->saved_buffers, &saved_buffer->link);
    }
}

void view_save_buffer(struct sway_view *view) {
    if (!sway_assert(wl_list_empty(&view->saved_buffers), "Didn't expect saved buffer")) {
        view_remove_saved_buffer(view);
    }
    view_for_each_surface(view, view_save_buffer_iterator, view);
}

void view_remove_saved_buffer(struct sway_view *view) {
    if (!sway_assert(!wl_list_empty(&view->saved_buffers), "Expected a saved buffer")) {
        return;
    }
    struct sway_saved_buffer *saved_buf, *tmp;
    wl_list_for_each_safe(saved_buf, tmp, &view->saved_buffers, link) {
        wlr_buffer_unlock(&saved_buf->buffer->base);
        wl_list_remove(&saved_buf->link);
        free(saved_buf);
    }
}

bool view_is_visible(struct sway_view *view) {
    if (view->window->node.destroying) {
        return false;
    }
    struct sway_output *output = view->window->output;
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

uint32_t view_configure(struct sway_view *view, double lx, double ly, int width,
        int height) {
    if (view->impl->configure) {
        return view->impl->configure(view, lx, ly, width, height);
    }
    return 0;
}
