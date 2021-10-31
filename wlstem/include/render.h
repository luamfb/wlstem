#ifndef WLSTEM_RENDER_H_
#define WLSTEM_RENDER_H_

#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>

void scissor_output(struct wlr_output *wlr_output,
        pixman_box32_t *rect);

void render_texture(struct wlr_output *wlr_output,
        pixman_region32_t *output_damage, struct wlr_texture *texture,
        const struct wlr_fbox *src_box, const struct wlr_box *dst_box,
        const float matrix[static 9], float alpha);

#endif /* WLSTEM_RENDER_H_ */
