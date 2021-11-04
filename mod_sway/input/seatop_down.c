#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <float.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_tablet_v2.h>
#include "sway_config.h"
#include "cursor.h"
#include "seat.h"
#include "view.h"
#include "log.h"

struct seatop_down_event {
    struct wls_window *win;
    double ref_lx, ref_ly;         // cursor's x/y at start of op
    double ref_win_lx, ref_win_ly; // window's x/y at start of op
};

void down_handle_pointer_axis(struct sway_seat *seat,
        struct wlr_event_pointer_axis *event) {
    struct sway_input_device *input_device =
        event->device ? event->device->data : NULL;
    struct input_config *ic =
        input_device ? input_device_get_config(input_device) : NULL;
    float scroll_factor =
        (ic == NULL || ic->scroll_factor == FLT_MIN) ? 1.0f : ic->scroll_factor;

    wlr_seat_pointer_notify_axis(seat->wlr_seat, event->time_msec,
        event->orientation, scroll_factor * event->delta,
        round(scroll_factor * event->delta_discrete), event->source);
}

void down_handle_button(struct sway_seat *seat, uint32_t time_msec,
        struct wlr_input_device *device, uint32_t button,
        enum wlr_button_state state) {
    seat_pointer_notify_button(seat, time_msec, button, state);

    if (seat->cursor->pressed_button_count == 0) {
        seatop_begin_default(seat);
    }
}

void down_handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
    struct seatop_down_event *e = seat->seatop_data;
    struct wls_window *win = e->win;
    if (seat_is_input_allowed(seat, win->view->surface)) {
        double moved_x = seat->cursor->cursor->x - e->ref_lx;
        double moved_y = seat->cursor->cursor->y - e->ref_ly;
        double sx = e->ref_win_lx + moved_x;
        double sy = e->ref_win_ly + moved_y;
        wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
    }
}

void down_handle_tablet_tool_tip(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec,
        enum wlr_tablet_tool_tip_state state) {
    if (state == WLR_TABLET_TOOL_TIP_UP) {
        wlr_tablet_v2_tablet_tool_notify_up(tool->tablet_v2_tool);
        seatop_begin_default(seat);
    }
}

void down_handle_tablet_tool_motion(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec) {
    struct seatop_down_event *e = seat->seatop_data;
    struct wls_window *win = e->win;
    if (seat_is_input_allowed(seat, win->view->surface)) {
        double moved_x = seat->cursor->cursor->x - e->ref_lx;
        double moved_y = seat->cursor->cursor->y - e->ref_ly;
        double sx = e->ref_win_lx + moved_x;
        double sy = e->ref_win_ly + moved_y;
        wlr_tablet_v2_tablet_tool_notify_motion(tool->tablet_v2_tool, sx, sy);
    }
}

void down_handle_unref(struct sway_seat *seat, struct wls_window *win) {
    struct seatop_down_event *e = seat->seatop_data;
    if (e->win == win) {
        seatop_begin_default(seat);
    }
}

static const struct sway_seatop_impl seatop_impl = {
};

void seatop_begin_down(struct sway_seat *seat, struct wls_window *win,
        uint32_t time_msec, int sx, int sy) {
    seatop_end(seat);

    seat->cursor_pressed = true;

    struct seatop_down_event *e =
        calloc(1, sizeof(struct seatop_down_event));
    if (!e) {
        return;
    }
    e->win = win;
    e->ref_lx = seat->cursor->cursor->x;
    e->ref_ly = seat->cursor->cursor->y;
    e->ref_win_lx = sx;
    e->ref_win_ly = sy;

    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;
}
