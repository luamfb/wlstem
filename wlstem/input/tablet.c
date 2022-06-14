#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_pad.h>
#include "log.h"
#include "cursor.h"
#include "seat.h"
#include "tablet.h"
#include "wlstem.h"

static void handle_pad_tablet_destroy(struct wl_listener *listener, void *data) {
    struct sway_tablet_pad *pad =
        wl_container_of(listener, pad, tablet_destroy);

    pad->tablet = NULL;

    wl_list_remove(&pad->tablet_destroy.link);
    wl_list_init(&pad->tablet_destroy.link);
}

void attach_tablet_pad(struct sway_tablet_pad *tablet_pad,
        struct sway_tablet *tablet) {
    sway_log(SWAY_DEBUG, "Attaching tablet pad \"%s\" to tablet tool \"%s\"",
        tablet_pad->seat_device->input_device->wlr_device->name,
        tablet->seat_device->input_device->wlr_device->name);

    tablet_pad->tablet = tablet;

    wl_list_remove(&tablet_pad->tablet_destroy.link);
    tablet_pad->tablet_destroy.notify = handle_pad_tablet_destroy;
    wl_signal_add(&tablet->seat_device->input_device->wlr_device->events.destroy,
        &tablet_pad->tablet_destroy);
}

struct sway_tablet *sway_tablet_create(struct sway_seat *seat,
        struct sway_seat_device *device) {
    struct sway_tablet *tablet =
        calloc(1, sizeof(struct sway_tablet));
    if (!sway_assert(tablet, "could not allocate sway tablet for seat")) {
        return NULL;
    }

    wl_list_insert(&seat->cursor->tablets, &tablet->link);

    device->tablet = tablet;
    tablet->seat_device = device;

    return tablet;
}

void sway_tablet_destroy(struct sway_tablet *tablet) {
    if (!tablet) {
        return;
    }
    wl_list_remove(&tablet->link);
    free(tablet);
}

static void handle_tablet_pad_attach(struct wl_listener *listener,
        void *data) {
    struct sway_tablet_pad *pad = wl_container_of(listener, pad, attach);
    struct wlr_tablet_tool *wlr_tool = data;
    struct sway_tablet_tool *tool = wlr_tool->data;

    if (!tool) {
        return;
    }

    attach_tablet_pad(pad, tool->tablet);
}

static void handle_tablet_pad_ring(struct wl_listener *listener, void *data) {
    struct sway_tablet_pad *pad = wl_container_of(listener, pad, ring);
    struct wlr_event_tablet_pad_ring *event = data;

    if (!pad->current_surface) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_ring(pad->tablet_v2_pad,
        event->ring, event->position,
        event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER,
        event->time_msec);
}

static void handle_tablet_pad_strip(struct wl_listener *listener, void *data) {
    struct sway_tablet_pad *pad = wl_container_of(listener, pad, strip);
    struct wlr_event_tablet_pad_strip *event = data;

    if (!pad->current_surface) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_strip(pad->tablet_v2_pad,
        event->strip, event->position,
        event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
        event->time_msec);
}

static void handle_tablet_pad_button(struct wl_listener *listener, void *data) {
    struct sway_tablet_pad *pad = wl_container_of(listener, pad, button);
    struct wlr_event_tablet_pad_button *event = data;

    if (!pad->current_surface) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_mode(pad->tablet_v2_pad,
        event->group, event->mode, event->time_msec);

    wlr_tablet_v2_tablet_pad_notify_button(pad->tablet_v2_pad,
        event->button, event->time_msec,
        (enum zwp_tablet_pad_v2_button_state)event->state);
}

struct sway_tablet_pad *sway_tablet_pad_create(struct sway_seat *seat,
        struct sway_seat_device *device) {
    struct sway_tablet_pad *tablet_pad =
        calloc(1, sizeof(struct sway_tablet_pad));
    if (!sway_assert(tablet_pad, "could not allocate sway tablet")) {
        return NULL;
    }

    tablet_pad->seat_device = device;
    wl_list_init(&tablet_pad->attach.link);
    wl_list_init(&tablet_pad->button.link);
    wl_list_init(&tablet_pad->strip.link);
    wl_list_init(&tablet_pad->ring.link);
    wl_list_init(&tablet_pad->surface_destroy.link);
    wl_list_init(&tablet_pad->tablet_destroy.link);

    wl_list_insert(&seat->cursor->tablet_pads, &tablet_pad->link);

    return tablet_pad;
}

void sway_configure_tablet_pad(struct sway_tablet_pad *tablet_pad) {
    struct wlr_input_device *device =
        tablet_pad->seat_device->input_device->wlr_device;
    struct sway_seat *seat = tablet_pad->seat_device->sway_seat;

    if (!tablet_pad->tablet_v2_pad) {
        tablet_pad->tablet_v2_pad =
            wlr_tablet_pad_create(wls->tablet_v2, seat->wlr_seat, device);
    }

    wl_list_remove(&tablet_pad->attach.link);
    tablet_pad->attach.notify = handle_tablet_pad_attach;
    wl_signal_add(&device->tablet_pad->events.attach_tablet,
        &tablet_pad->attach);

    wl_list_remove(&tablet_pad->button.link);
    tablet_pad->button.notify = handle_tablet_pad_button;
    wl_signal_add(&device->tablet_pad->events.button, &tablet_pad->button);

    wl_list_remove(&tablet_pad->strip.link);
    tablet_pad->strip.notify = handle_tablet_pad_strip;
    wl_signal_add(&device->tablet_pad->events.strip, &tablet_pad->strip);

    wl_list_remove(&tablet_pad->ring.link);
    tablet_pad->ring.notify = handle_tablet_pad_ring;
    wl_signal_add(&device->tablet_pad->events.ring, &tablet_pad->ring);

    /* Search for a sibling tablet */
    if (!wlr_input_device_is_libinput(device)) {
        /* We can only do this on libinput devices */
        return;
    }

    struct libinput_device_group *group =
        libinput_device_get_device_group(wlr_libinput_get_device_handle(device));
    struct sway_tablet *tool;
    wl_list_for_each(tool, &seat->cursor->tablets, link) {
        struct wlr_input_device *tablet =
            tool->seat_device->input_device->wlr_device;
        if (!wlr_input_device_is_libinput(tablet)) {
            continue;
        }

        struct libinput_device_group *tablet_group =
            libinput_device_get_device_group(wlr_libinput_get_device_handle(tablet));

        if (tablet_group == group) {
            attach_tablet_pad(tablet_pad, tool);
            break;
        }
    }
}

void sway_tablet_pad_destroy(struct sway_tablet_pad *tablet_pad) {
    if (!tablet_pad) {
        return;
    }

    wl_list_remove(&tablet_pad->link);
    wl_list_remove(&tablet_pad->attach.link);
    wl_list_remove(&tablet_pad->button.link);
    wl_list_remove(&tablet_pad->strip.link);
    wl_list_remove(&tablet_pad->ring.link);
    wl_list_remove(&tablet_pad->surface_destroy.link);
    wl_list_remove(&tablet_pad->tablet_destroy.link);

    free(tablet_pad);
}

static void handle_pad_tablet_surface_destroy(struct wl_listener *listener,
        void *data) {
    struct sway_tablet_pad *tablet_pad =
        wl_container_of(listener, tablet_pad, surface_destroy);

    wlr_tablet_v2_tablet_pad_notify_leave(tablet_pad->tablet_v2_pad,
        tablet_pad->current_surface);
    wl_list_remove(&tablet_pad->surface_destroy.link);
    wl_list_init(&tablet_pad->surface_destroy.link);
    tablet_pad->current_surface = NULL;
}

void sway_tablet_pad_notify_enter(struct sway_tablet_pad *tablet_pad,
        struct wlr_surface *surface) {
    if (!tablet_pad || !tablet_pad->tablet) {
        return;
    }

    if (surface == tablet_pad->current_surface) {
        return;
    }

    /* Leave current surface */
    if (tablet_pad->current_surface) {
        wlr_tablet_v2_tablet_pad_notify_leave(tablet_pad->tablet_v2_pad,
            tablet_pad->current_surface);
        wl_list_remove(&tablet_pad->surface_destroy.link);
        wl_list_init(&tablet_pad->surface_destroy.link);
        tablet_pad->current_surface = NULL;
    }

    if (!wlr_surface_accepts_tablet_v2(tablet_pad->tablet->tablet_v2, surface)) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_enter(tablet_pad->tablet_v2_pad,
        tablet_pad->tablet->tablet_v2, surface);

    tablet_pad->current_surface = surface;
    wl_list_remove(&tablet_pad->surface_destroy.link);
    tablet_pad->surface_destroy.notify = handle_pad_tablet_surface_destroy;
    wl_signal_add(&surface->events.destroy, &tablet_pad->surface_destroy);
}
