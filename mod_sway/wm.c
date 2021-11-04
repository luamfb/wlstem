#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "transaction.h"
#include "seat.h"
#include "server_arrange.h"
#include "server_wm.h"
#include "sway_server.h"
#include "window_title.h"
#include "window.h"
#include "output.h"
#include "view.h"
#include "layers.h"
#include "list.h"
#include "log.h"
#include "wlstem.h"

static void wm_handle_output_layout_change(
        struct wl_listener *listener, void *data) {
    arrange_output_layout();
}

static void wm_handle_output_connected(
        struct wl_listener *listener, void *data) {
    struct sway_output *output = data;
    arrange_layers(output);

    seize_containers_from_noop_output(output);

    // Set each seat's focus if not already set
    struct sway_seat *seat = NULL;
    wl_list_for_each(seat, &wls->seats, link) {
        if (!seat->has_focus) {
            seat_set_focus_output(seat, output);
        }
    }

    arrange_root();
}

static void wm_handle_output_disconnected(
        struct wl_listener *listener, void *data) {
    arrange_root();
}

static void wm_handle_output_mode_changed(
        struct wl_listener *listener, void *data) {
    struct sway_output *output = data;
    arrange_layers(output);
    arrange_output(output);
}

static void title_handle_container_destroyed(
        struct wl_listener *listener, void *data) {

    struct window_title *title =
        wl_container_of(listener, title, container_destroyed);

    free(title->formatted_title);
    wlr_texture_destroy(title->title_focused);
    wlr_texture_destroy(title->title_unfocused);
    wlr_texture_destroy(title->title_urgent);
    free(title);
}

static void title_handle_scale_change(
        struct wl_listener *listener, void *data) {
    struct sway_container *con = data;
    container_update_title_textures(con);
}

static void wm_handle_new_window(
        struct wl_listener *listener, void *data) {
    struct sway_container *container = data;
    struct window_title *title_data =
        calloc(1, sizeof(struct window_title));
    if (!title_data) {
        sway_log(SWAY_ERROR, "failed to allocate memory for window title data");
        return;
    }

    wl_signal_add(&container->events.destroy, &title_data->container_destroyed);
    title_data->container_destroyed.notify = title_handle_container_destroyed;

    wl_signal_add(&container->events.scale_change, &title_data->scale_changed);
    title_data->scale_changed.notify = title_handle_scale_change;

    container->data = title_data;
}

struct server_wm * server_wm_create(void) {
    struct server_wm *wm = calloc(1, sizeof(struct server_wm));
    if (!wm) {
        return NULL;
    }
    wl_signal_add(&wls->output_manager->events.output_layout_changed,
        &wm->output_layout_change);
    wm->output_layout_change.notify = wm_handle_output_layout_change;

    wl_signal_add(&wls->output_manager->events.output_connected,
        &wm->output_connected);
    wm->output_connected.notify = wm_handle_output_connected;

    wl_signal_add(&wls->output_manager->events.output_disconnected,
        &wm->output_disconnected);
    wm->output_disconnected.notify = wm_handle_output_disconnected;

    wl_signal_add(&wls->output_manager->events.output_mode_changed,
        &wm->output_mode_changed);
    wm->output_mode_changed.notify = wm_handle_output_mode_changed;

    wl_signal_add(&wls->events.new_window, &wm->new_window);
    wm->new_window.notify = wm_handle_new_window;

    return wm;
}

void server_wm_destroy(struct server_wm *wm) {
    if (!wm) {
        return;
    }
    free(wm);
}
