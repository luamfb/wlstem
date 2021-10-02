#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output_layout.h>
#include "cairo.h"
#include "pango.h"
#include "sway/sway_config.h"
#include "output.h"
#include "list.h"
#include "log.h"
#include "sway/view.h"
#include "wlstem.h"

struct sway_container *container_create(struct sway_view *view) {
    struct sway_container *c = calloc(1, sizeof(struct sway_container));
    if (!c) {
        sway_log(SWAY_ERROR, "Unable to allocate sway_container");
        return NULL;
    }
    node_init(&c->node, N_CONTAINER, c);
    c->view = view;
    c->alpha = 1.0f;

    c->outputs = create_list();

    wl_signal_init(&c->events.destroy);
    wl_signal_emit(&wls->node_manager->events.new_node, &c->node);

    return c;
}

void container_destroy(struct sway_container *con) {
    if (!sway_assert(con->node.destroying,
                "Tried to free container which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(con->node.ntxnrefs == 0, "Tried to free container "
                "which is still referenced by transactions")) {
        return;
    }
    free(con->title);
    free(con->formatted_title);
    wlr_texture_destroy(con->title_focused);
    wlr_texture_destroy(con->title_unfocused);
    wlr_texture_destroy(con->title_urgent);
    list_free(con->outputs);

    if (con->view) {
        if (con->view->container == con) {
            con->view->container = NULL;
        }
        if (con->view->destroying) {
            view_destroy(con->view);
        }
    }

    free(con);
}

void container_begin_destroy(struct sway_container *con) {
    wl_signal_emit(&con->node.events.destroy, &con->node);

    container_end_mouse_operation(con);

    con->node.destroying = true;
    node_set_dirty(&con->node);

    if (con->output) {
        container_detach(con);
    }
}

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

static struct sway_container *container_at_linear(struct wls_transaction_node *parent,
        double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    list_t *children = node_get_children(parent);
    if (children) {
        for (int i = 0; i < children->length; ++i) {
            struct sway_container *child = children->items[i];
            struct sway_container *container =
                tiling_container_at(&child->node, lx, ly, surface, sx, sy);
            if (container) {
                return container;
            }
        }
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

struct sway_container *tiling_container_at(struct wls_transaction_node *parent,
        double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    if (node_is_view(parent)) {
        return view_container_at(parent, lx, ly, surface, sx, sy);
    }
    if (!node_get_children(parent)) {
        return NULL;
    }
    if (node_may_have_container_children(parent)) {
        return container_at_linear(parent, lx, ly, surface, sx, sy);
    }
    return NULL;
}

bool surface_is_popup(struct wlr_surface *surface) {
    if (wlr_surface_is_xdg_surface(surface)) {
        struct wlr_xdg_surface *xdg_surface =
            wlr_xdg_surface_from_wlr_surface(surface);
        while (xdg_surface && xdg_surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
            if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
                return true;
            }
            xdg_surface = xdg_surface->toplevel->parent;
        }
        return false;
    }

    return false;
}

void container_damage_whole(struct sway_container *container) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        output_damage_whole_container(output, container);
    }
}

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct sway_output *container_get_effective_output(struct sway_container *con) {
    if (con->outputs->length == 0) {
        return NULL;
    }
    return con->outputs->items[con->outputs->length - 1];
}

static void update_title_texture(struct sway_container *con,
        struct wlr_texture **texture, struct border_colors *class) {
    struct sway_output *output = container_get_effective_output(con);
    if (!output) {
        return;
    }
    if (*texture) {
        wlr_texture_destroy(*texture);
        *texture = NULL;
    }
    if (!con->formatted_title) {
        return;
    }

    double scale = output->wlr_output->scale;
    int width = 0;
    int height = con->title_height * scale;

    // We must use a non-nil cairo_t for cairo_set_font_options to work.
    // Therefore, we cannot use cairo_create(NULL).
    cairo_surface_t *dummy_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, 0, 0);
    cairo_t *c = cairo_create(dummy_surface);
    cairo_set_antialias(c, CAIRO_ANTIALIAS_BEST);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
    if (output->wlr_output->subpixel == WL_OUTPUT_SUBPIXEL_NONE) {
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
    } else {
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
        cairo_font_options_set_subpixel_order(fo,
            to_cairo_subpixel_order(output->wlr_output->subpixel));
    }
    cairo_set_font_options(c, fo);
    get_text_size(c, config->font, &width, NULL, NULL, scale,
            config->pango_markup, "%s", con->formatted_title);
    cairo_surface_destroy(dummy_surface);
    cairo_destroy(c);

    cairo_surface_t *surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cairo = cairo_create(surface);
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
    cairo_set_font_options(cairo, fo);
    cairo_font_options_destroy(fo);
    cairo_set_source_rgba(cairo, class->background[0], class->background[1],
            class->background[2], class->background[3]);
    cairo_paint(cairo);
    PangoContext *pango = pango_cairo_create_context(cairo);
    cairo_set_source_rgba(cairo, class->text[0], class->text[1],
            class->text[2], class->text[3]);
    cairo_move_to(cairo, 0, 0);

    pango_printf(cairo, config->font, scale, config->pango_markup,
            "%s", con->formatted_title);

    cairo_surface_flush(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    struct wlr_renderer *renderer = wlr_backend_get_renderer(
            output->wlr_output->backend);
    *texture = wlr_texture_from_pixels(
            renderer, WL_SHM_FORMAT_ARGB8888, stride, width, height, data);
    cairo_surface_destroy(surface);
    g_object_unref(pango);
    cairo_destroy(cairo);
}

void container_update_title_textures(struct sway_container *container) {
    update_title_texture(container, &container->title_focused,
            &config->border_colors.focused);
    update_title_texture(container, &container->title_unfocused,
            &config->border_colors.unfocused);
    update_title_texture(container, &container->title_urgent,
            &config->border_colors.urgent);
    container_damage_whole(container);
}

void container_calculate_title_height(struct sway_container *container) {
    if (!container->formatted_title) {
        container->title_height = 0;
        return;
    }
    cairo_t *cairo = cairo_create(NULL);
    int height;
    int baseline;
    get_text_size(cairo, config->font, NULL, &height, &baseline, 1,
            config->pango_markup, "%s", container->formatted_title);
    cairo_destroy(cairo);
    container->title_height = height;
    container->title_baseline = baseline;
}

size_t container_titlebar_height(void) {
    return config->font_height + config->titlebar_v_padding * 2;
}


/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void container_set_resizing(struct sway_container *con, bool resizing) {
    if (!con) {
        return;
    }

    if (con->view) {
        if (con->view->impl->set_resizing) {
            con->view->impl->set_resizing(con->view, resizing);
        }
    }
}

void container_get_box(struct sway_container *container, struct wlr_box *box) {
    box->x = container->x;
    box->y = container->y;
    box->width = container->width;
    box->height = container->height;
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
        int x, int y, void *data) {
    struct wlr_output *wlr_output = data;
    wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
        int x, int y, void *data) {
    struct wlr_output *wlr_output = data;
    wlr_surface_send_leave(surface, wlr_output);
}

void container_discover_outputs(struct sway_container *con) {
    struct wlr_box con_box = {
        .x = con->current.x,
        .y = con->current.y,
        .width = con->current.width,
        .height = con->current.height,
    };
    struct sway_output *old_output = container_get_effective_output(con);

    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        struct wlr_box output_box;
        output_get_box(output, &output_box);
        struct wlr_box intersection;
        bool intersects =
            wlr_box_intersection(&intersection, &con_box, &output_box);
        int index = list_find(con->outputs, output);

        if (intersects && index == -1) {
            // Send enter
            sway_log(SWAY_DEBUG, "Container %p entered output %p", con, output);
            if (con->view) {
                view_for_each_surface(con->view,
                        surface_send_enter_iterator, output->wlr_output);
                if (con->view->foreign_toplevel) {
                    wlr_foreign_toplevel_handle_v1_output_enter(
                            con->view->foreign_toplevel, output->wlr_output);
                }
            }
            list_add(con->outputs, output);
        } else if (!intersects && index != -1) {
            // Send leave
            sway_log(SWAY_DEBUG, "Container %p left output %p", con, output);
            if (con->view) {
                view_for_each_surface(con->view,
                    surface_send_leave_iterator, output->wlr_output);
                if (con->view->foreign_toplevel) {
                    wlr_foreign_toplevel_handle_v1_output_leave(
                            con->view->foreign_toplevel, output->wlr_output);
                }
            }
            list_del(con->outputs, index);
        }
    }
    struct sway_output *new_output = container_get_effective_output(con);
    double old_scale = old_output && old_output->enabled ?
        old_output->wlr_output->scale : -1;
    double new_scale = new_output ? new_output->wlr_output->scale : -1;
    if (old_scale != new_scale) {
        container_update_title_textures(con);
    }
}

list_t *container_get_siblings(struct sway_container *container) {
    return container->output->tiling;
}

int container_sibling_index(struct sway_container *child) {
    return list_find(container_get_siblings(child), child);
}

list_t *container_get_current_siblings(struct sway_container *container) {
    struct sway_output *current_output = container->current.output;
    if (!current_output) {
        sway_log(SWAY_ERROR, "container has no current output!");
        assert(false);
    }
    return current_output->current.tiling;
}

void container_add_sibling(struct sway_container *fixed,
        struct sway_container *active, bool after) {
    if (active->output) {
        container_detach(active);
    }
    list_t *siblings = container_get_siblings(fixed);
    int index = list_find(siblings, fixed);
    list_insert(siblings, index + after, active);
    active->output = fixed->output;
}

void container_detach(struct sway_container *child) {
    struct sway_output *old_output = child->output;
    list_t *siblings = container_get_siblings(child);
    if (siblings) {
        int index = list_find(siblings, child);
        if (index != -1) {
            list_del(siblings, index);
        }
    }
    child->output = NULL;

    if (old_output) {
        node_set_dirty(&old_output->node);
    }
    node_set_dirty(&child->node);
}

void container_replace(struct sway_container *container,
        struct sway_container *replacement) {
    if (container->output) {
        container_add_sibling(container, replacement, 1);
        container_detach(container);
    }
}
