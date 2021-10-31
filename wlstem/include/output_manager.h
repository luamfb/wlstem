#ifndef WLS_OUTPUT_MANAGER_H_
#define WLS_OUTPUT_MANAGER_H_
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/render/wlr_texture.h>
#include "container.h"
#include "config.h"
#include "list.h"

struct wls_server;

struct wls_output_manager {
    struct wlr_output_layout *output_layout;

#if HAVE_XWAYLAND
    struct wl_list xwayland_unmanaged; // sway_xwayland_unmanaged::link
#endif
    struct wl_list drag_icons; // sway_drag_icon::link

    // Includes disabled outputs
    struct wl_list all_outputs; // sway_output::link

    double x, y;
    double width, height;

    struct wlr_output_manager_v1 *output_manager_v1;
    struct wl_listener output_manager_apply;
    struct wl_listener output_manager_test;

    struct wl_listener new_output;
    list_t *outputs; // struct sway_output

    // For when there's no connected outputs
    struct sway_output *noop_output;

    struct {
        struct wl_signal output_layout_changed;
        struct wl_signal output_connected;
        struct wl_signal output_disconnected;
        struct wl_signal output_mode_changed;
    } events;
};

void handle_new_output(struct wl_listener *listener, void *data);

struct wls_output_manager *wls_output_manager_create(struct wls_server *server);

void wls_output_manager_destroy(struct wls_output_manager *output_manager);

void wls_output_layout_get_box(struct wls_output_manager *output_manager, struct wlr_box *box);

void wls_update_output_manager_config(struct wls_output_manager *output_manager);

#endif
