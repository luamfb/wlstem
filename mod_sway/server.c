#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/noop.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include "config.h"
#include "list.h"
#include "log.h"
#include "sway/sway_config.h"
#include "sway/desktop/idle_inhibit_v1.h"
#include "sway/input/input-manager.h"
#include "sway/tree/arrange.h"
#include "output.h"
#include "sway/server.h"
#include "root.h"
#if HAVE_XWAYLAND
#include "sway/xwayland.h"
#endif
#include "wlstem.h"
#include "wls_server.h"

bool server_init(struct sway_server *server) {
    sway_log(SWAY_DEBUG, "Initializing Wayland server");

    struct wlr_renderer *renderer = wlr_backend_get_renderer(wls->server->backend);
    assert(renderer);

    wlr_renderer_init_wl_display(renderer, wls->server->wl_display);

    server->compositor = wlr_compositor_create(wls->server->wl_display, renderer);
    server->compositor_new_surface.notify = handle_compositor_new_surface;
    wl_signal_add(&server->compositor->events.new_surface,
        &server->compositor_new_surface);

    server->data_device_manager =
        wlr_data_device_manager_create(wls->server->wl_display);

    wlr_gamma_control_manager_v1_create(wls->server->wl_display);

    server->new_output.notify = handle_new_output;
    wl_signal_add(&wls->server->backend->events.new_output, &server->new_output);
    server->output_layout_change.notify = handle_output_layout_change;
    wl_signal_add(&wls->root->output_layout->events.change,
        &server->output_layout_change);

    wlr_xdg_output_manager_v1_create(wls->server->wl_display, wls->root->output_layout);

    server->idle = wlr_idle_create(wls->server->wl_display);
    server->idle_inhibit_manager_v1 =
        sway_idle_inhibit_manager_v1_create(wls->server->wl_display, server->idle);

    server->layer_shell = wlr_layer_shell_v1_create(wls->server->wl_display);
    wl_signal_add(&server->layer_shell->events.new_surface,
        &server->layer_shell_surface);
    server->layer_shell_surface.notify = handle_layer_shell_surface;

    server->xdg_shell = wlr_xdg_shell_create(wls->server->wl_display);
    wl_signal_add(&server->xdg_shell->events.new_surface,
        &server->xdg_shell_surface);
    server->xdg_shell_surface.notify = handle_xdg_shell_surface;

    server->tablet_v2 = wlr_tablet_v2_create(wls->server->wl_display);

    server->server_decoration_manager =
        wlr_server_decoration_manager_create(wls->server->wl_display);
    wlr_server_decoration_manager_set_default_mode(
        server->server_decoration_manager,
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    wl_signal_add(&server->server_decoration_manager->events.new_decoration,
        &server->server_decoration);
    server->server_decoration.notify = handle_server_decoration;
    wl_list_init(&server->decorations);

    server->xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(wls->server->wl_display);
    wl_signal_add(
            &server->xdg_decoration_manager->events.new_toplevel_decoration,
            &server->xdg_decoration);
    server->xdg_decoration.notify = handle_xdg_decoration;
    wl_list_init(&server->xdg_decorations);

    server->relative_pointer_manager =
        wlr_relative_pointer_manager_v1_create(wls->server->wl_display);

    server->pointer_constraints =
        wlr_pointer_constraints_v1_create(wls->server->wl_display);
    server->pointer_constraint.notify = handle_pointer_constraint;
    wl_signal_add(&server->pointer_constraints->events.new_constraint,
        &server->pointer_constraint);

    server->presentation =
        wlr_presentation_create(wls->server->wl_display, wls->server->backend);

    server->output_power_manager_v1 =
        wlr_output_power_manager_v1_create(wls->server->wl_display);
    server->output_power_manager_set_mode.notify =
        handle_output_power_manager_set_mode;
    wl_signal_add(&server->output_power_manager_v1->events.set_mode,
        &server->output_power_manager_set_mode);
    server->input_method = wlr_input_method_manager_v2_create(wls->server->wl_display);
    server->text_input = wlr_text_input_manager_v3_create(wls->server->wl_display);
    server->foreign_toplevel_manager =
        wlr_foreign_toplevel_manager_v1_create(wls->server->wl_display);

    wlr_export_dmabuf_manager_v1_create(wls->server->wl_display);
    wlr_screencopy_manager_v1_create(wls->server->wl_display);
    wlr_data_control_manager_v1_create(wls->server->wl_display);
    wlr_primary_selection_v1_device_manager_create(wls->server->wl_display);
    wlr_viewporter_create(wls->server->wl_display);

    // Avoid using "wayland-0" as display socket
    char name_candidate[16];
    for (int i = 1; i <= 32; ++i) {
        sprintf(name_candidate, "wayland-%d", i);
        if (wl_display_add_socket(wls->server->wl_display, name_candidate) >= 0) {
            server->socket = strdup(name_candidate);
            break;
        }
    }

    if (!server->socket) {
        sway_log(SWAY_ERROR, "Unable to open wayland socket");
        wlr_backend_destroy(wls->server->backend);
        return false;
    }

    server->noop_backend = wlr_noop_backend_create(wls->server->wl_display);

    struct wlr_output *wlr_output = wlr_noop_add_output(server->noop_backend);
    wls->root->noop_output = output_create(wlr_output);

    server->headless_backend =
        wlr_headless_backend_create_with_renderer(wls->server->wl_display, renderer);
    if (!server->headless_backend) {
        sway_log(SWAY_INFO, "Failed to create secondary headless backend, "
            "starting without it");
    } else {
        wlr_multi_backend_add(wls->server->backend, server->headless_backend);
    }

    // This may have been set already via -Dtxn-timeout
    if (!server->txn_timeout_ms) {
        server->txn_timeout_ms = 200;
    }

    server->transactions = create_list();

    server->input = input_manager_create(server);
    input_manager_get_default_seat(); // create seat0

    server->wm = server_wm_create();

    return true;
}

bool server_start(struct sway_server *server) {
#if HAVE_XWAYLAND
    if (config->xwayland != XWAYLAND_MODE_DISABLED) {
        sway_log(SWAY_DEBUG, "Initializing Xwayland (lazy=%d)",
                config->xwayland == XWAYLAND_MODE_LAZY);
        server->xwayland.wlr_xwayland =
            wlr_xwayland_create(wls->server->wl_display, server->compositor,
                    config->xwayland == XWAYLAND_MODE_LAZY);
        if (!server->xwayland.wlr_xwayland) {
            sway_log(SWAY_ERROR, "Failed to start Xwayland");
            unsetenv("DISPLAY");
        } else {
            wl_signal_add(&server->xwayland.wlr_xwayland->events.new_surface,
                &server->xwayland_surface);
            server->xwayland_surface.notify = handle_xwayland_surface;
            wl_signal_add(&server->xwayland.wlr_xwayland->events.ready,
                &server->xwayland_ready);
            server->xwayland_ready.notify = handle_xwayland_ready;

            setenv("DISPLAY", server->xwayland.wlr_xwayland->display_name, true);

            /* xcursor configured by the default seat */
        }
    }
#endif

    sway_log(SWAY_INFO, "Starting backend on wayland display '%s'",
            server->socket);
    if (!wlr_backend_start(wls->server->backend)) {
        sway_log(SWAY_ERROR, "Failed to start backend");
        wlr_backend_destroy(wls->server->backend);
        return false;
    }
    return true;
}
