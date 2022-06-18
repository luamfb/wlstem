#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include <wayland-server-core.h>
#include <GLES2/gl2.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/util/region.h>
#include "log.h"
#include "output.h"
#include "output_config.h"
#include "wlstem.h"

void premultiply_alpha(float color[4], float opacity) {
    color[3] *= opacity;
    color[0] *= color[3];
    color[1] *= color[3];
    color[2] *= color[3];
}

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

void render_rect(struct sway_output *output,
        pixman_region32_t *output_damage, const struct wlr_box *_box,
        float color[static 4]) {
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_renderer *renderer =
        wlr_backend_get_renderer(wlr_output->backend);

    struct wlr_box box;
    memcpy(&box, _box, sizeof(struct wlr_box));
    box.x -= output->lx * wlr_output->scale;
    box.y -= output->ly * wlr_output->scale;

    pixman_region32_t damage;
    pixman_region32_init(&damage);
    pixman_region32_union_rect(&damage, &damage, box.x, box.y,
        box.width, box.height);
    pixman_region32_intersect(&damage, &damage, output_damage);
    bool damaged = pixman_region32_not_empty(&damage);
    if (!damaged) {
        goto damage_finish;
    }

    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
    for (int i = 0; i < nrects; ++i) {
        scissor_output(wlr_output, &rects[i]);
        wlr_render_rect(renderer, &box, color,
            wlr_output->transform_matrix);
    }

damage_finish:
    pixman_region32_fini(&damage);
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

void output_render(struct sway_output *output, struct timespec *when,
        pixman_region32_t *damage) {
    struct wlr_output *wlr_output = output->wlr_output;

    struct wlr_renderer *renderer =
        wlr_backend_get_renderer(wlr_output->backend);
    if (!sway_assert(renderer != NULL,
            "expected the output backend to have a renderer")) {
        return;
    }

    if (!output->current.active) {
        return;
    }

    wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);

    if (!pixman_region32_not_empty(damage)) {
        // Output isn't damaged but needs buffer swap
        goto renderer_end;
    }

    if (wls->debug.damage == DAMAGE_HIGHLIGHT) {
        wlr_renderer_clear(renderer, (float[]){1, 1, 0, 1});
    } else if (wls->debug.damage == DAMAGE_RERENDER) {
        int width, height;
        wlr_output_transformed_resolution(wlr_output, &width, &height);
        pixman_region32_union_rect(damage, damage, 0, 0, width, height);
    }

    if (!output_has_opaque_overlay_layer_surface(output)) {
        wls->user_callbacks.output_render_non_overlay(output, renderer, damage);
    }
    wls->user_callbacks.output_render_overlay(output, renderer, damage);

renderer_end:
    wlr_renderer_scissor(renderer, NULL);
    wlr_output_render_software_cursors(wlr_output, damage);
    wlr_renderer_end(renderer);

    int width, height;
    wlr_output_transformed_resolution(wlr_output, &width, &height);

    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);

    enum wl_output_transform transform =
        wlr_output_transform_invert(wlr_output->transform);
    wlr_region_transform(&frame_damage, &output->damage->current,
        transform, width, height);

    if (wls->debug.damage == DAMAGE_HIGHLIGHT) {
        pixman_region32_union_rect(&frame_damage, &frame_damage,
            0, 0, wlr_output->width, wlr_output->height);
    }

    wlr_output_set_damage(wlr_output, &frame_damage);
    pixman_region32_fini(&frame_damage);

    if (!wlr_output_commit(wlr_output)) {
        return;
    }
    output->last_frame = *when;
}
