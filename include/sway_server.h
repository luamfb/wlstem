#ifndef _SWAY_SERVER_H
#define _SWAY_SERVER_H
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "config.h"
#include "list.h"
#if HAVE_XWAYLAND
#include "sway_xwayland.h"
#endif

struct server_wm;

struct sway_server {
    const char *socket;

    struct wlr_backend *noop_backend;
    // secondary headless backend used for creating virtual outputs on-the-fly
    struct wlr_backend *headless_backend;

    struct wlr_compositor *compositor;
    struct wl_listener compositor_new_surface;

    struct wlr_data_device_manager *data_device_manager;

    struct sway_input_manager *input;

    struct wl_listener output_layout_change;

    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener layer_shell_surface;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener xdg_shell_surface;

#if HAVE_XWAYLAND
    struct sway_xwayland xwayland;
    struct wl_listener xwayland_surface;
    struct wl_listener xwayland_ready;
#endif

    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

    struct wlr_server_decoration_manager *server_decoration_manager;
    struct wl_listener server_decoration;
    struct wl_list decorations; // sway_server_decoration::link

    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wl_listener xdg_decoration;
    struct wl_list xdg_decorations; // sway_xdg_decoration::link

    struct wlr_presentation *presentation;

    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wl_listener pointer_constraint;

    struct wlr_output_power_manager_v1 *output_power_manager_v1;
    struct wl_listener output_power_manager_set_mode;
    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

    struct server_wm *wm;
};

extern struct sway_server server;

/* Prepares an unprivileged server_init by performing all privileged operations in advance */
bool server_privileged_prepare(struct sway_server *server);
bool server_init(struct sway_server *server);
bool server_start(struct sway_server *server);

void handle_compositor_new_surface(struct wl_listener *listener, void *data);

void handle_idle_inhibitor_v1(struct wl_listener *listener, void *data);
void handle_layer_shell_surface(struct wl_listener *listener, void *data);
void handle_xdg_shell_surface(struct wl_listener *listener, void *data);
#if HAVE_XWAYLAND
void handle_xwayland_surface(struct wl_listener *listener, void *data);
#endif
void handle_server_decoration(struct wl_listener *listener, void *data);
void handle_xdg_decoration(struct wl_listener *listener, void *data);
void handle_pointer_constraint(struct wl_listener *listener, void *data);

#endif
