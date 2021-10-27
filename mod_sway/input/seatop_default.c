#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <float.h>
#include <libevdev/libevdev.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "sway_config.h"
#include "cursor.h"
#include "seat.h"
#include "tablet.h"
#include "output.h"
#include "view.h"
#include "sway_server.h"
#include "log.h"
#if HAVE_XWAYLAND
#include "sway_xwayland.h"
#endif
#include "wlstem.h"

struct seatop_default_event {
    struct wls_transaction_node *previous_node;
    uint32_t pressed_buttons[SWAY_CURSOR_PRESSED_BUTTONS_CAP];
    size_t pressed_button_count;
};

/**
 * Remove a button (and duplicates) from the sorted list of currently pressed
 * buttons.
 */
static void state_erase_button(struct seatop_default_event *e,
        uint32_t button) {
    size_t j = 0;
    for (size_t i = 0; i < e->pressed_button_count; ++i) {
        if (i > j) {
            e->pressed_buttons[j] = e->pressed_buttons[i];
        }
        if (e->pressed_buttons[i] != button) {
            ++j;
        }
    }
    while (e->pressed_button_count > j) {
        --e->pressed_button_count;
        e->pressed_buttons[e->pressed_button_count] = 0;
    }
}

/**
 * Add a button to the sorted list of currently pressed buttons, if there
 * is space.
 */
static void state_add_button(struct seatop_default_event *e, uint32_t button) {
    if (e->pressed_button_count >= SWAY_CURSOR_PRESSED_BUTTONS_CAP) {
        return;
    }
    size_t i = 0;
    while (i < e->pressed_button_count && e->pressed_buttons[i] < button) {
        ++i;
    }
    size_t j = e->pressed_button_count;
    while (j > i) {
        e->pressed_buttons[j] = e->pressed_buttons[j - 1];
        --j;
    }
    e->pressed_buttons[i] = button;
    e->pressed_button_count++;
}

/*-------------------------------------------\
 * Functions used by handle_tablet_tool_tip  /
 *-----------------------------------------*/

static void handle_tablet_tool_tip(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec,
        enum wlr_tablet_tool_tip_state state) {
    if (state == WLR_TABLET_TOOL_TIP_UP) {
        wlr_tablet_v2_tablet_tool_notify_up(tool->tablet_v2_tool);
        return;
    }

    struct sway_cursor *cursor = seat->cursor;
    struct wlr_surface *surface = NULL;
    double sx, sy;
    struct wls_transaction_node *node = node_at_coords(seat,
        cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

    if (!sway_assert(surface,
            "Expected null-surface tablet input to route through pointer emulation")) {
        return;
    }

    struct sway_container *cont = node && node->type == N_CONTAINER ?
        node->sway_container : NULL;

    if (wlr_surface_is_layer_surface(surface)) {
        // Handle tapping a layer surface
        struct wlr_layer_surface_v1 *layer =
                wlr_layer_surface_v1_from_wlr_surface(surface);
        if (layer->current.keyboard_interactive) {
            seat_set_focus_layer(seat, layer);
        }
    } else if (cont) {
        // Handle tapping on a container surface
        seat_set_focus_container(seat, cont);
        seatop_begin_down(seat, node->sway_container, time_msec, sx, sy);
    }
#if HAVE_XWAYLAND
    // Handle tapping on an xwayland unmanaged view
    else if (wlr_surface_is_xwayland_surface(surface)) {
        struct wlr_xwayland_surface *xsurface =
                wlr_xwayland_surface_from_wlr_surface(surface);
        if (xsurface->override_redirect &&
                wlr_xwayland_or_surface_wants_focus(xsurface)) {
            struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
            wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
            seat_set_focus_surface(seat, xsurface->surface, false);
        }
    }
#endif

    wlr_tablet_v2_tablet_tool_notify_down(tool->tablet_v2_tool);
    wlr_tablet_tool_v2_start_implicit_grab(tool->tablet_v2_tool);
}

/*----------------------------------\
 * Functions used by handle_button  /
 *--------------------------------*/

static void add_or_remove_button_to_state(struct sway_seat *seat,
        struct wlr_input_device *device, uint32_t button,
        enum wlr_button_state state) {
    // We can reach this for non-pointer devices if we're currently emulating
    // pointer input for one. Emulated input should not trigger bindings. The
    // device can be NULL if this is synthetic (e.g. swaymsg-generated) input.
    if (device && device->type != WLR_INPUT_DEVICE_POINTER) {
        return;
    }

    struct seatop_default_event *e = seat->seatop_data;
    if (state == WLR_BUTTON_PRESSED) {
        state_add_button(e, button);
    } else {
        state_erase_button(e, button);
    }
}

void default_handle_button(struct sway_seat *seat, uint32_t time_msec,
        struct wlr_input_device *device, uint32_t button,
        enum wlr_button_state state) {
    struct sway_cursor *cursor = seat->cursor;

    // Determine what's under the cursor
    struct wlr_surface *surface = NULL;
    double sx, sy;
    struct wls_transaction_node *node = node_at_coords(seat,
            cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

    struct sway_container *cont = node && node->type == N_CONTAINER ?
        node->sway_container : NULL;

    add_or_remove_button_to_state(seat, device, button, state);

    // Handle clicking an empty output
    if (node && node->type == N_OUTPUT) {
        if (state == WLR_BUTTON_PRESSED) {
            seat_set_focus(seat, node);
        }
        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

    // Handle clicking a layer surface
    if (surface && wlr_surface_is_layer_surface(surface)) {
        struct wlr_layer_surface_v1 *layer =
            wlr_layer_surface_v1_from_wlr_surface(surface);
        if (layer->current.keyboard_interactive) {
            seat_set_focus_layer(seat, layer);
        }
        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

    // Handle mousedown on a container surface
    if (surface && cont && state == WLR_BUTTON_PRESSED) {
        seat_set_focus_container(seat, cont);
        seatop_begin_down(seat, cont, time_msec, sx, sy);
        seat_pointer_notify_button(seat, time_msec, button, WLR_BUTTON_PRESSED);
        return;
    }

    // Handle clicking a container surface or decorations
    if (cont && state == WLR_BUTTON_PRESSED) {
        node = seat_get_focus_inactive(seat, &cont->node);
        seat_set_focus(seat, node);
        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

#if HAVE_XWAYLAND
    // Handle clicking on xwayland unmanaged view
    if (surface && wlr_surface_is_xwayland_surface(surface)) {
        struct wlr_xwayland_surface *xsurface =
            wlr_xwayland_surface_from_wlr_surface(surface);
        if (xsurface->override_redirect &&
                wlr_xwayland_or_surface_wants_focus(xsurface)) {
            struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
            wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
            seat_set_focus_surface(seat, xsurface->surface, false);
            seat_pointer_notify_button(seat, time_msec, button, state);
            return;
        }
    }
#endif

    seat_pointer_notify_button(seat, time_msec, button, state);
}

/*------------------------------------------\
 * Functions used by handle_pointer_motion  /
 *----------------------------------------*/

static void check_focus_follows_mouse(struct sway_seat *seat,
        struct seatop_default_event *e, struct wls_transaction_node *hovered_node) {
    struct wls_transaction_node *focus = seat_get_focus(seat);

    // This is the case if a layer-shell surface is hovered.
    // If it's on another output, focus the active workspace there.
    if (!hovered_node) {
        struct wlr_output *wlr_output = wlr_output_layout_output_at(
                wls->output_manager->output_layout, seat->cursor->cursor->x, seat->cursor->cursor->y);
        if (wlr_output == NULL) {
            return;
        }
        struct sway_output *hovered_output = wlr_output->data;
        if (focus && hovered_output != node_get_output(focus)) {
            seat_set_focus(seat, &hovered_output->node);
        }
        return;
    }

    // If an output node is hovered (eg. in the gap area), only set focus if
    // the output is different than the previous focus.
    if (focus && hovered_node->type == N_OUTPUT) {
        struct sway_output *focused_output = node_get_output(focus);
        struct sway_output *hovered_output = node_get_output(hovered_node);
        if (hovered_output != focused_output) {
            seat_set_focus(seat, seat_get_focus_inactive(seat, hovered_node));
        }
        return;
    }

    // This is where we handle the common case. We don't want to focus inactive
    // tabs, hence the view_is_visible check.
    if (node_is_view(hovered_node) &&
            view_is_visible(hovered_node->sway_container->view)) {
        // e->previous_node is the node which the cursor was over previously.
        // If focus_follows_mouse is yes and the cursor got over the view due
        // to, say, a workspace switch, we don't want to set the focus.
        // But if focus_follows_mouse is "always", we do.
        if (hovered_node != e->previous_node ||
                config->focus_follows_mouse == FOLLOWS_ALWAYS) {
            seat_set_focus(seat, hovered_node);
        }
    }
}

void default_handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct sway_cursor *cursor = seat->cursor;

    struct wlr_surface *surface = NULL;
    double sx, sy;
    struct wls_transaction_node *node = node_at_coords(seat,
            cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

    if (config->focus_follows_mouse != FOLLOWS_NO) {
        check_focus_follows_mouse(seat, e, node);
    }

    if (surface) {
        if (seat_is_input_allowed(seat, surface)) {
            wlr_seat_pointer_notify_enter(seat->wlr_seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
        }
    } else {
        cursor_update_image(cursor, node);
        wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
    }

    struct sway_drag_icon *drag_icon;
    wl_list_for_each(drag_icon, &wls->output_manager->drag_icons, link) {
        if (drag_icon->seat == seat) {
            drag_icon_update_position(drag_icon);
        }
    }

    e->previous_node = node;
}

static void handle_tablet_tool_motion(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct sway_cursor *cursor = seat->cursor;

    struct wlr_surface *surface = NULL;
    double sx, sy;
    struct wls_transaction_node *node = node_at_coords(seat,
            cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

    if (config->focus_follows_mouse != FOLLOWS_NO) {
        check_focus_follows_mouse(seat, e, node);
    }

    if (surface) {
        if (seat_is_input_allowed(seat, surface)) {
            wlr_tablet_v2_tablet_tool_notify_proximity_in(tool->tablet_v2_tool,
                tool->tablet->tablet_v2, surface);
            wlr_tablet_v2_tablet_tool_notify_motion(tool->tablet_v2_tool, sx, sy);
        }
    } else {
        cursor_update_image(cursor, node);
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->tablet_v2_tool);
    }

    struct sway_drag_icon *drag_icon;
    wl_list_for_each(drag_icon, &wls->output_manager->drag_icons, link) {
        if (drag_icon->seat == seat) {
            drag_icon_update_position(drag_icon);
        }
    }

    e->previous_node = node;
}

/*----------------------------------------\
 * Functions used by handle_pointer_axis  /
 *--------------------------------------*/

static uint32_t wl_axis_to_button(struct wlr_event_pointer_axis *event) {
    switch (event->orientation) {
    case WLR_AXIS_ORIENTATION_VERTICAL:
        return event->delta < 0 ? SWAY_SCROLL_UP : SWAY_SCROLL_DOWN;
    case WLR_AXIS_ORIENTATION_HORIZONTAL:
        return event->delta < 0 ? SWAY_SCROLL_LEFT : SWAY_SCROLL_RIGHT;
    default:
        sway_log(SWAY_DEBUG, "Unknown axis orientation");
        return 0;
    }
}

static void handle_pointer_axis(struct sway_seat *seat,
        struct wlr_event_pointer_axis *event) {
    struct sway_input_device *input_device =
        event->device ? event->device->data : NULL;
    struct input_config *ic =
        input_device ? input_device_get_config(input_device) : NULL;
    struct sway_cursor *cursor = seat->cursor;
    struct seatop_default_event *e = seat->seatop_data;

    float scroll_factor =
        (ic == NULL || ic->scroll_factor == FLT_MIN) ? 1.0f : ic->scroll_factor;

    bool handled = false;
    uint32_t button = wl_axis_to_button(event);
    state_add_button(e, button);
    state_erase_button(e, button);

    if (!handled) {
        wlr_seat_pointer_notify_axis(cursor->seat->wlr_seat, event->time_msec,
            event->orientation, scroll_factor * event->delta,
            round(scroll_factor * event->delta_discrete), event->source);
    }
}

/*----------------------------------\
 * Functions used by handle_rebase  /
 *--------------------------------*/

static void handle_rebase(struct sway_seat *seat, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct sway_cursor *cursor = seat->cursor;
    struct wlr_surface *surface = NULL;
    double sx = 0.0, sy = 0.0;
    e->previous_node = node_at_coords(seat,
            cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

    if (surface) {
        if (seat_is_input_allowed(seat, surface)) {
            wlr_seat_pointer_notify_enter(seat->wlr_seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
        }
    } else {
        cursor_update_image(cursor, e->previous_node);
        wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
    }
}

static const struct sway_seatop_impl seatop_impl = {
    .pointer_axis = handle_pointer_axis,
    .tablet_tool_tip = handle_tablet_tool_tip,
    .tablet_tool_motion = handle_tablet_tool_motion,
    .rebase = handle_rebase,
};

void seatop_begin_default(struct sway_seat *seat) {
    seatop_end(seat);

    seat->cursor_pressed = false;

    struct seatop_default_event *e =
        calloc(1, sizeof(struct seatop_default_event));
    sway_assert(e, "Unable to allocate seatop_default_event");
    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    seatop_rebase(seat, 0);
}
