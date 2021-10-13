#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_pad.h>
#include "log.h"
#include "sway_config.h"
#include "cursor.h"
#include "seat.h"
#include "tablet.h"
#include "wlstem.h"

void sway_configure_tablet(struct sway_tablet *tablet) {
    struct wlr_input_device *device =
        tablet->seat_device->input_device->wlr_device;
    struct sway_seat *seat = tablet->seat_device->sway_seat;

    if ((seat->wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER) == 0) {
        seat_configure_xcursor(seat);
    }

    if (!tablet->tablet_v2) {
        tablet->tablet_v2 =
            wlr_tablet_create(wls->tablet_v2, seat->wlr_seat, device);
    }

    /* Search for a sibling tablet pad */
    if (!wlr_input_device_is_libinput(device)) {
        /* We can only do this on libinput devices */
        return;
    }

    struct libinput_device_group *group =
        libinput_device_get_device_group(wlr_libinput_get_device_handle(device));
    struct sway_tablet_pad *tablet_pad;
    wl_list_for_each(tablet_pad, &seat->cursor->tablet_pads, link) {
        struct wlr_input_device *pad_device =
            tablet_pad->seat_device->input_device->wlr_device;
        if (!wlr_input_device_is_libinput(pad_device)) {
            continue;
        }

        struct libinput_device_group *pad_group =
            libinput_device_get_device_group(wlr_libinput_get_device_handle(pad_device));

        if (pad_group == group) {
            attach_tablet_pad(tablet_pad, tablet);
            break;
        }
    }
}

static void handle_tablet_tool_set_cursor(struct wl_listener *listener, void *data) {
    struct sway_tablet_tool *tool =
        wl_container_of(listener, tool, set_cursor);
    struct wlr_tablet_v2_event_cursor *event = data;

    struct sway_cursor *cursor = tool->seat->cursor;
    if (!seatop_allows_set_cursor(cursor->seat)) {
        return;
    }

    struct wl_client *focused_client = NULL;
    struct wlr_surface *focused_surface = tool->tablet_v2_tool->focused_surface;
    if (focused_surface != NULL) {
        focused_client = wl_resource_get_client(focused_surface->resource);
    }

    // TODO: check cursor mode
    if (focused_client == NULL ||
            event->seat_client->client != focused_client) {
        sway_log(SWAY_DEBUG, "denying request to set cursor from unfocused client");
        return;
    }

    cursor_set_image_surface(cursor, event->surface, event->hotspot_x,
            event->hotspot_y, focused_client);
}

static void handle_tablet_tool_destroy(struct wl_listener *listener, void *data) {
    struct sway_tablet_tool *tool =
        wl_container_of(listener, tool, tool_destroy);

    wl_list_remove(&tool->tool_destroy.link);
    wl_list_remove(&tool->set_cursor.link);

    free(tool);
}

void sway_tablet_tool_configure(struct sway_tablet *tablet,
        struct wlr_tablet_tool *wlr_tool) {
    struct sway_tablet_tool *tool =
        calloc(1, sizeof(struct sway_tablet_tool));
    if (!sway_assert(tool, "could not allocate sway tablet tool for tablet")) {
        return;
    }

    switch (wlr_tool->type) {
    case WLR_TABLET_TOOL_TYPE_LENS:
    case WLR_TABLET_TOOL_TYPE_MOUSE:
        tool->mode = SWAY_TABLET_TOOL_MODE_RELATIVE;
        break;
    default:
        tool->mode = SWAY_TABLET_TOOL_MODE_ABSOLUTE;

        struct input_config *ic = input_device_get_config(
            tablet->seat_device->input_device);
        if (!ic) {
            break;
        }

        for (int i = 0; i < ic->tools->length; i++) {
            struct input_config_tool *tool_config = ic->tools->items[i];
            if (tool_config->type == wlr_tool->type) {
                tool->mode = tool_config->mode;
                break;
            }
        }
    }

    tool->seat = tablet->seat_device->sway_seat;
    tool->tablet = tablet;
    tool->tablet_v2_tool =
        wlr_tablet_tool_create(wls->tablet_v2,
            tablet->seat_device->sway_seat->wlr_seat, wlr_tool);

    tool->tool_destroy.notify = handle_tablet_tool_destroy;
    wl_signal_add(&wlr_tool->events.destroy, &tool->tool_destroy);

    tool->set_cursor.notify = handle_tablet_tool_set_cursor;
    wl_signal_add(&tool->tablet_v2_tool->events.set_cursor,
        &tool->set_cursor);

    wlr_tool->data = tool;
}
