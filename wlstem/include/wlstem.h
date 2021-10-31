#ifndef WLSTEM_WLSTEM_H
#define WLSTEM_WLSTEM_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_tablet_v2.h>
#include "list.h"
#include "node.h"
#include "misc_protocols.h"
#include "output_manager.h"

struct sway_output;

typedef void (*handle_output_commit_fn)(struct sway_output *output,
        struct wlr_output_event_commit *event);

typedef void (*output_render_fn)(struct sway_output *output,
        struct wlr_renderer *renderer,
        pixman_region32_t *damage);

struct wls_debug {
    bool noatomic;         // Ignore atomic layout updates
    bool txn_timings;      // Log verbose messages about transactions
    bool txn_wait;         // Always wait for the timeout before applying

    enum {
        DAMAGE_DEFAULT,    // Default behaviour
        DAMAGE_HIGHLIGHT,  // Highlight regions of the screen being damaged
        DAMAGE_RERENDER,   // Render the full output when any damage occurs
    } damage;

    size_t transaction_timeout_ms; // 0 means use default timeout
};

struct wls_context {
    struct wls_server *server;
    struct wls_node_manager *node_manager;
    struct wls_output_manager *output_manager;
    list_t *output_configs;
    struct wls_input_method_manager *input_method_manager;
    struct wlr_tablet_manager_v2 *tablet_v2;
    struct wl_list seats;
    struct sway_seat *current_seat;
    struct wls_misc_protocols *misc_protocols;

    // user-provided callbacks
    handle_output_commit_fn handle_output_commit;

    // for the overlay in layer shell protocol
    output_render_fn output_render_overlay;
    // for all remaining layers in layer shell protocol
    output_render_fn output_render_non_overlay;

    struct wls_debug debug;
    struct {
        struct wl_signal new_window;
    } events;
};

// The context for wlstem.
// This is a NULL pointer prior to calling `wls_init`
// (and also after calling `wls_fini`).
extern struct wls_context *wls;

// Initializes the `wls` variable containing wlstem's context.
// Returns whether or not the initialization succeeded.
//
bool wls_init(handle_output_commit_fn, output_render_fn, output_render_fn);

// Finalizes the wlstem context, freeing up any resources it used so far.
void wls_fini(void);

#endif
