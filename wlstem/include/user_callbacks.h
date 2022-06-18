#ifndef WLSTEM_USER_CALLBACKS_H
#define WLSTEM_USER_CALLBACKS_H

#include <stdbool.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include "output.h"

typedef void (*handle_output_commit_fn)(
    struct sway_output *output,
    struct wlr_output_event_commit *event);

typedef void (*output_render_fn)(
    struct sway_output *output,
    struct wlr_renderer *renderer,
    pixman_region32_t *damage);

typedef struct sway_output * (*choose_absorber_output_fn)(
    struct sway_output *giver);

struct wls_user_callbacks {
    // user-provided callbacks
    handle_output_commit_fn handle_output_commit;

    // for the overlay in layer shell protocol
    output_render_fn output_render_overlay;
    // for all remaining layers in layer shell protocol
    output_render_fn output_render_non_overlay;

    // this function must return which output will receive the windows from
    // another output (`giver`), that has been disconnected.
    // If NULL is returned, the `noop_output` is used, and the window will
    // disappear (until it is moved to a "real" output).
    choose_absorber_output_fn choose_absorber_output;
};

bool validate_callbacks(const struct wls_user_callbacks *callbacks);

#endif /* WLSTEM_USER_CALLBACKS_H */
