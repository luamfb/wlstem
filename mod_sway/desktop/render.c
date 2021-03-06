#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/region.h>
#include "log.h"
#include "config.h"
#include "foreach.h"
#include "sway_config.h"
#include "input_manager.h"
#include "seat.h"
#include "layers.h"
#include "output.h"
#include "render.h"
#include "sway_server.h"
#include "server_wm.h"
#include "window_title.h"
#include "window.h"
#include "output_manager.h"
#include "view.h"
#include "wlstem.h"

struct render_data {
    pixman_region32_t *damage;
    float alpha;
};

/**
 * Apply scale to a width or height.
 *
 * One does not simply multiply the width by the scale. We allow fractional
 * scaling, which means the resulting scaled width might be a decimal.
 * So we round it.
 *
 * But even this can produce undesirable results depending on the X or Y offset
 * of the box. For example, with a scale of 1.5, a box with width=1 should not
 * scale to 2px if its X coordinate is 1, because the X coordinate would have
 * scaled to 2px.
 */
static int scale_length(int length, int offset, float scale) {
    return round((offset + length) * scale) - round(offset * scale);
}

static void render_surface_iterator(struct sway_output *output, struct sway_view *view,
        struct wlr_surface *surface, struct wlr_box *_box, float rotation,
        void *_data) {
    struct render_data *data = _data;
    struct wlr_output *wlr_output = output->wlr_output;
    pixman_region32_t *output_damage = data->damage;
    float alpha = data->alpha;

    struct wlr_texture *texture = wlr_surface_get_texture(surface);
    if (!texture) {
        return;
    }

    struct wlr_fbox src_box;
    wlr_surface_get_buffer_source_box(surface, &src_box);

    struct wlr_box dst_box = *_box;
    scale_box(&dst_box, wlr_output->scale);

    float matrix[9];
    enum wl_output_transform transform =
        wlr_output_transform_invert(surface->current.transform);
    wlr_matrix_project_box(matrix, &dst_box, transform, rotation,
        wlr_output->transform_matrix);

    render_texture(wlr_output, output_damage, texture,
        &src_box, &dst_box, matrix, alpha);

    wlr_presentation_surface_sampled_on_output(server.presentation, surface,
        wlr_output);
}

static void render_layer_toplevel(struct sway_output *output,
        pixman_region32_t *damage, struct wl_list *layer_surfaces) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_layer_for_each_toplevel_surface(output, layer_surfaces,
        render_surface_iterator, &data);
}

static void render_layer_popups(struct sway_output *output,
        pixman_region32_t *damage, struct wl_list *layer_surfaces) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_layer_for_each_popup_surface(output, layer_surfaces,
        render_surface_iterator, &data);
}

#if HAVE_XWAYLAND
static void render_unmanaged(struct sway_output *output,
        pixman_region32_t *damage, struct wl_list *unmanaged) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_unmanaged_for_each_surface(output, unmanaged,
        render_surface_iterator, &data);
}
#endif

static void render_drag_icons(struct sway_output *output,
        pixman_region32_t *damage, struct wl_list *drag_icons) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_drag_icons_for_each_surface(output, drag_icons,
        render_surface_iterator, &data);
}

static void render_view_toplevels(struct sway_view *view,
        struct sway_output *output, pixman_region32_t *damage, float alpha) {
    struct render_data data = {
        .damage = damage,
        .alpha = alpha,
    };
    // Render all toplevels without descending into popups
    double ox = view->window->surface_x -
        output->lx - view->geometry.x;
    double oy = view->window->surface_y -
        output->ly - view->geometry.y;
    output_surface_for_each_surface(output, view->surface, ox, oy,
            render_surface_iterator, &data);
}

static void render_view_popups(struct sway_view *view,
        struct sway_output *output, pixman_region32_t *damage, float alpha) {
    struct render_data data = {
        .damage = damage,
        .alpha = alpha,
    };
    output_view_for_each_popup_surface(output, view,
        render_surface_iterator, &data);
}

static void render_saved_view(struct sway_view *view,
        struct sway_output *output, pixman_region32_t *damage, float alpha) {
    struct wlr_output *wlr_output = output->wlr_output;

    if (wl_list_empty(&view->saved_buffers)) {
        return;
    }
    struct sway_saved_buffer *saved_buf;
    wl_list_for_each(saved_buf, &view->saved_buffers, link) {
        if (!saved_buf->buffer->texture) {
            continue;
        }

        struct wlr_box box = {
            .x = view->window->surface_x - output->lx -
                view->saved_geometry.x + saved_buf->x,
            .y = view->window->surface_y - output->ly -
                view->saved_geometry.y + saved_buf->y,
            .width = saved_buf->width,
            .height = saved_buf->height,
        };

        struct wlr_box output_box = {
            .width = output->width,
            .height = output->height,
        };

        struct wlr_box intersection;
        bool intersects = wlr_box_intersection(&intersection, &output_box, &box);
        if (!intersects) {
            continue;
        }

        scale_box(&box, wlr_output->scale);

        float matrix[9];
        enum wl_output_transform transform = wlr_output_transform_invert(saved_buf->transform);
        wlr_matrix_project_box(matrix, &box, transform, 0,
            wlr_output->transform_matrix);

        render_texture(wlr_output, damage, saved_buf->buffer->texture,
            &saved_buf->source_box, &box, matrix, alpha);
    }

    // FIXME: we should set the surface that this saved buffer originates from
    // as sampled here.
    // https://github.com/swaywm/sway/pull/4465#discussion_r321082059
}

/**
 * Render a view's surface and left/bottom/right borders.
 */
static void render_view(struct sway_output *output, pixman_region32_t *damage,
        struct wls_window *win, struct border_colors *colors) {
    struct sway_view *view = win->view;
    if (!wl_list_empty(&view->saved_buffers)) {
        render_saved_view(view, output, damage, view->window->alpha);
    } else if (view->surface) {
        render_view_toplevels(view, output, damage, view->window->alpha);
    }

    struct wlr_box box;
    float output_scale = output->wlr_output->scale;
    float color[4];
    struct wls_window_state *state = &win->current;

    // left border
    {
        memcpy(&color, colors->child_border, sizeof(float) * 4);
        premultiply_alpha(color, win->alpha);
        box.x = state->x;
        box.y = state->content_y;
        box.width = config->border_thickness;
        box.height = state->content_height;
        scale_box(&box, output_scale);
        render_rect(output, damage, &box, color);
    }

    list_t *siblings = window_get_current_siblings(win);

    // right border
    {
        if (siblings->length == 1) {
            memcpy(&color, colors->indicator, sizeof(float) * 4);
        } else {
            memcpy(&color, colors->child_border, sizeof(float) * 4);
        }
        premultiply_alpha(color, win->alpha);
        box.x = state->content_x + state->content_width;
        box.y = state->content_y;
        box.width = config->border_thickness;
        box.height = state->content_height;
        scale_box(&box, output_scale);
        render_rect(output, damage, &box, color);
    }

    // bottom border
    {
        memcpy(&color, colors->child_border, sizeof(float) * 4);
        premultiply_alpha(color, win->alpha);
        box.x = state->x;
        box.y = state->content_y + state->content_height;
        box.width = state->width;
        box.height = config->border_thickness;
        scale_box(&box, output_scale);
        render_rect(output, damage, &box, color);
    }
}

/**
 * Render a titlebar.
 *
 * Care must be taken not to render over the same pixel multiple times,
 * otherwise the colors will be incorrect when using opacity.
 *
 * The height is: 1px border, 3px padding, font height, 3px padding, 1px border
 * The left side is: 1px border, 2px padding, title
 */
static void render_titlebar(struct sway_output *output,
        pixman_region32_t *output_damage, struct wls_window *win,
        int x, int y, int width,
        struct border_colors *colors, struct wlr_texture *title_texture) {
    struct wlr_box box;
    float color[4];
    float output_scale = output->wlr_output->scale;
    double output_x = output->lx;
    double output_y = output->ly;
    int titlebar_border_thickness = config->titlebar_border_thickness;
    int titlebar_h_padding = config->titlebar_h_padding;
    int titlebar_v_padding = config->titlebar_v_padding;
    enum alignment title_align = config->title_align;

    // Single pixel bar above title
    memcpy(&color, colors->border, sizeof(float) * 4);
    premultiply_alpha(color, win->alpha);
    box.x = x;
    box.y = y;
    box.width = width;
    box.height = titlebar_border_thickness;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    // Single pixel bar below title
    box.x = x;
    box.y = y + window_titlebar_height() - titlebar_border_thickness;
    box.width = width;
    box.height = titlebar_border_thickness;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    // Single pixel left edge
    box.x = x;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_border_thickness;
    box.height = window_titlebar_height() - titlebar_border_thickness * 2;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    // Single pixel right edge
    box.x = x + width - titlebar_border_thickness;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_border_thickness;
    box.height = window_titlebar_height() - titlebar_border_thickness * 2;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    int inner_x = x - output_x + titlebar_h_padding;
    int bg_y = y + titlebar_border_thickness;
    size_t inner_width = width - titlebar_h_padding * 2;

    // output-buffer local
    int ob_inner_x = round(inner_x * output_scale);
    int ob_inner_width = scale_length(inner_width, inner_x, output_scale);
    int ob_bg_height = scale_length(
            (titlebar_v_padding - titlebar_border_thickness) * 2 +
            config->font_height, bg_y, output_scale);

    // Title text
    int ob_title_x = 0;  // output-buffer-local
    int ob_title_width = 0; // output-buffer-local
    if (title_texture) {
        struct wlr_box texture_box;
        wlr_texture_get_size(title_texture,
            &texture_box.width, &texture_box.height);
        ob_title_width = texture_box.width;

        // The title texture might be shorter than the config->font_height,
        // in which case we need to pad it above and below.
        int ob_padding_above = round((config->font_baseline -
                    win->title_baseline + titlebar_v_padding -
                    titlebar_border_thickness) * output_scale);
        int ob_padding_below = ob_bg_height - ob_padding_above -
            texture_box.height;

        // Render texture
        if (texture_box.width > ob_inner_width) {
            texture_box.x = ob_inner_x;
        } else if (title_align == ALIGN_LEFT) {
            texture_box.x = ob_inner_x;
        } else if (title_align == ALIGN_CENTER) {
            texture_box.x = ob_inner_x + ob_inner_width / 2
                - texture_box.width / 2;
        } else {
            texture_box.x = ob_inner_x + ob_inner_width - texture_box.width;
        }
        ob_title_x = texture_box.x;

        texture_box.y =
            round((bg_y - output_y) * output_scale) + ob_padding_above;

        float matrix[9];
        wlr_matrix_project_box(matrix, &texture_box,
            WL_OUTPUT_TRANSFORM_NORMAL,
            0.0, output->wlr_output->transform_matrix);

        if (ob_inner_width < texture_box.width) {
            texture_box.width = ob_inner_width;
        }

        render_texture(output->wlr_output, output_damage, title_texture,
            NULL, &texture_box, matrix, win->alpha);

        // Padding above
        memcpy(&color, colors->background, sizeof(float) * 4);
        premultiply_alpha(color, win->alpha);
        box.x = texture_box.x + round(output_x * output_scale);
        box.y = round((y + titlebar_border_thickness) * output_scale);
        box.width = texture_box.width;
        box.height = ob_padding_above;
        render_rect(output, output_damage, &box, color);

        // Padding below
        box.y += ob_padding_above + texture_box.height;
        box.height = ob_padding_below;
        render_rect(output, output_damage, &box, color);
    }

    // Determine the left + right extends of the textures (output-buffer local)
    int ob_left_x, ob_left_width, ob_right_x, ob_right_width;
    if (ob_title_width == 0) {
        ob_left_x = ob_inner_x;
        ob_left_width = 0;
        ob_right_x = ob_inner_x;
        ob_right_width = 0;
    } else {
        ob_left_x = 0;
        ob_left_width = 0;
        ob_right_x = ob_title_x;
        ob_right_width = ob_title_width;
    }
    if (ob_left_x < ob_inner_x) {
        ob_left_x = ob_inner_x;
    } else if (ob_left_x + ob_left_width > ob_right_x + ob_right_width) {
        ob_right_x = ob_left_x;
        ob_right_width = ob_left_width;
    }

    // Padding on left side
    box.x = x + titlebar_border_thickness;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_h_padding - titlebar_border_thickness;
    box.height = (titlebar_v_padding - titlebar_border_thickness) * 2 +
        config->font_height;
    scale_box(&box, output_scale);
    int left_x = ob_left_x + round(output_x * output_scale);
    if (box.x + box.width < left_x) {
        box.width += left_x - box.x - box.width;
    }
    render_rect(output, output_damage, &box, color);

    // Padding on right side
    box.x = x + width - titlebar_h_padding;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_h_padding - titlebar_border_thickness;
    box.height = (titlebar_v_padding - titlebar_border_thickness) * 2 +
        config->font_height;
    scale_box(&box, output_scale);
    int right_rx = ob_right_x + ob_right_width + round(output_x * output_scale);
    if (right_rx < box.x) {
        box.width += box.x - right_rx;
        box.x = right_rx;
    }
    render_rect(output, output_damage, &box, color);
}

struct parent_data {
    struct wlr_box box;
    list_t *children;
    bool focused;
};

static void render_window(struct sway_output *output,
    pixman_region32_t *damage, struct wls_window *win, bool parent_focused);

/**
 * Render a window's children using a L_HORIZ layout.
 *
 * Wrap child views in borders and leave child windows borderless because
 * they'll apply their own borders to their children.
 */
static void render_windows_linear(struct sway_output *output,
        pixman_region32_t *damage, struct parent_data *parent) {
    for (int i = 0; i < parent->children->length; ++i) {
        struct wls_window *child = parent->children->items[i];
        struct window_title *child_title = child->data;

        if (child->view) {
            struct sway_view *view = child->view;
            struct border_colors *colors;
            struct wlr_texture *title_texture;
            struct wls_window_state *state = &child->current;

            if (view_is_urgent(view)) {
                colors = &config->border_colors.urgent;
                title_texture = child_title->title_urgent;
            } else if (state->focused || parent->focused) {
                colors = &config->border_colors.focused;
                title_texture = child_title->title_focused;
            } else {
                colors = &config->border_colors.unfocused;
                title_texture = child_title->title_unfocused;
            }

            render_titlebar(output, damage, child, state->x,
                    state->y, state->width, colors,
                    title_texture);
            render_view(output, damage, child, colors);
        } else {
            render_window(output, damage, child,
                    parent->focused || child->current.focused);
        }
    }
}

static void render_window(struct sway_output *output,
        pixman_region32_t *damage, struct wls_window *win, bool focused) {
    struct parent_data data = {
        .box = {
            .x = win->current.x,
            .y = win->current.y,
            .width = win->current.width,
            .height = win->current.height,
        },
        .focused = focused,
    };
    render_windows_linear(output, damage, &data);
}

static void render_windows_in_output(struct sway_output *output,
        pixman_region32_t *damage) {
    struct parent_data data = {
        .box = {
            .x = output->current.render_lx,
            .y = output->current.render_ly,
            .width = output->usable_area.width,
            .height = output->usable_area.height,
        },
        .children = output->current.windows,
        .focused = false,
    };
    render_windows_linear(output, damage, &data);
}

void output_render_overlay(struct sway_output *output,
        struct wlr_renderer *renderer,
        pixman_region32_t *damage) {

    render_layer_toplevel(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
    render_layer_popups(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
    render_drag_icons(output, damage, &wls->output_manager->drag_icons);
}

void output_render_non_overlay(struct sway_output *output,
        struct wlr_renderer *renderer,
        pixman_region32_t *damage) {

    struct wlr_output *wlr_output = output->wlr_output;

    float clear_color[] = {0.25f, 0.25f, 0.25f, 1.0f};

    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
    for (int i = 0; i < nrects; ++i) {
        scissor_output(wlr_output, &rects[i]);
        wlr_renderer_clear(renderer, clear_color);
    }

    render_layer_toplevel(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
    render_layer_toplevel(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);

    render_windows_in_output(output, damage);
#if HAVE_XWAYLAND
    render_unmanaged(output, damage, &wls->output_manager->xwayland_unmanaged);
#endif
    render_layer_toplevel(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);

    render_layer_popups(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
    render_layer_popups(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);
    render_layer_popups(output, damage,
        &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);

    struct sway_seat *seat = input_manager_current_seat();
    struct wls_window *focus = seat_get_focused_window(seat);
    if (focus && focus->view) {
        render_view_popups(focus->view, output, damage, focus->alpha);
    }
}
