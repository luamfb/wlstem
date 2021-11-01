#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "cairo.h"
#include "pango.h"
#include "damage.h"
#include "foreach.h"
#include "sway_config.h"
#include "window_title.h"
#include "output.h"
#include "list.h"
#include "log.h"
#include "view.h"
#include "wlstem.h"

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
    struct window_title *title_data = con->data;
    if (!title_data->formatted_title) {
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
            config->pango_markup, "%s", title_data->formatted_title);
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
            "%s", title_data->formatted_title);

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
    struct window_title *title_data = container->data;
    update_title_texture(container, &title_data->title_focused,
            &config->border_colors.focused);
    update_title_texture(container, &title_data->title_unfocused,
            &config->border_colors.unfocused);
    update_title_texture(container, &title_data->title_urgent,
            &config->border_colors.urgent);
    container_damage_whole(container);
}

void container_calculate_title_height(struct sway_container *container) {
    struct window_title *title_data = container->data;
    if (!title_data->formatted_title) {
        container->title_height = 0;
        return;
    }
    cairo_t *cairo = cairo_create(NULL);
    int height;
    int baseline;
    get_text_size(cairo, config->font, NULL, &height, &baseline, 1,
            config->pango_markup, "%s", title_data->formatted_title);
    cairo_destroy(cairo);
    container->title_height = height;
    container->title_baseline = baseline;
}

size_t container_titlebar_height(void) {
    return config->font_height + config->titlebar_v_padding * 2;
}
