#ifndef _SWAY_ROOT_H
#define _SWAY_ROOT_H
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/render/wlr_texture.h>
#include "container.h"
#include "config.h"
#include "list.h"

struct wls_server;

struct sway_root {
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

    list_t *outputs; // struct sway_output

    // For when there's no connected outputs
    struct sway_output *noop_output;

    struct {
        struct wl_signal output_layout_changed;
        struct wl_signal output_connected;
        struct wl_signal output_disconnected;
    } events;
};

struct sway_root *root_create(struct wls_server *server);

void root_destroy(struct sway_root *root);

void root_for_each_output(void (*f)(struct sway_output *output, void *data),
        void *data);

void root_for_each_container(void (*f)(struct sway_container *con, void *data),
        void *data);

void root_get_box(struct sway_root *root, struct wlr_box *box);

void update_output_manager_config(struct sway_root *root);

#endif
