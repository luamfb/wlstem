#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <wayland-server-core.h>
#include <GLES2/gl2.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include "output.h"
#include "output_config.h"

void scissor_output(struct wlr_output *wlr_output,
        pixman_box32_t *rect) {
    struct wlr_renderer *renderer = wlr_backend_get_renderer(wlr_output->backend);
    assert(renderer);

    struct wlr_box box = {
        .x = rect->x1,
        .y = rect->y1,
        .width = rect->x2 - rect->x1,
        .height = rect->y2 - rect->y1,
    };

    int ow, oh;
    wlr_output_transformed_resolution(wlr_output, &ow, &oh);

    enum wl_output_transform transform =
        wlr_output_transform_invert(wlr_output->transform);
    wlr_box_transform(&box, &box, transform, ow, oh);

    wlr_renderer_scissor(renderer, &box);
}

static void set_scale_filter(struct wlr_output *wlr_output,
        struct wlr_texture *texture, enum scale_filter_mode scale_filter) {
    if (!wlr_texture_is_gles2(texture)) {
        return;
    }

    struct wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(texture, &attribs);

    glBindTexture(attribs.target, attribs.tex);

    switch (scale_filter) {
    case SCALE_FILTER_LINEAR:
        glTexParameteri(attribs.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        break;
    case SCALE_FILTER_NEAREST:
        glTexParameteri(attribs.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        break;
    case SCALE_FILTER_DEFAULT:
    case SCALE_FILTER_SMART:
        assert(false); // unreachable
    }
}

void render_texture(struct wlr_output *wlr_output,
        pixman_region32_t *output_damage, struct wlr_texture *texture,
        const struct wlr_fbox *src_box, const struct wlr_box *dst_box,
        const float matrix[static 9], float alpha) {
    struct wlr_renderer *renderer =
        wlr_backend_get_renderer(wlr_output->backend);
    struct sway_output *output = wlr_output->data;

    struct wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(texture, &attribs);

    pixman_region32_t damage;
    pixman_region32_init(&damage);
    pixman_region32_union_rect(&damage, &damage, dst_box->x, dst_box->y,
        dst_box->width, dst_box->height);
    pixman_region32_intersect(&damage, &damage, output_damage);
    bool damaged = pixman_region32_not_empty(&damage);
    if (!damaged) {
        goto damage_finish;
    }

    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
    for (int i = 0; i < nrects; ++i) {
        scissor_output(wlr_output, &rects[i]);
        set_scale_filter(wlr_output, texture, output->scale_filter);
        if (src_box != NULL) {
            wlr_render_subtexture_with_matrix(renderer, texture, src_box, matrix, alpha);
        } else {
            wlr_render_texture_with_matrix(renderer, texture, matrix, alpha);
        }
    }

damage_finish:
    pixman_region32_fini(&damage);
}
