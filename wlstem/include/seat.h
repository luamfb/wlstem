#ifndef _SWAY_INPUT_SEAT_H
#define _SWAY_INPUT_SEAT_H

#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include "idle.h"
#include "input_manager.h"
#include "tablet.h"
#include "text_input.h"

struct seat_config;
struct wls_window;
struct sway_output;
struct sway_seat;

struct sway_seatop_impl {
    void (*rebase)(struct sway_seat *seat, uint32_t time_msec);
};

struct sway_seat_device {
    struct sway_seat *sway_seat;
    struct sway_input_device *input_device;
    struct sway_keyboard *keyboard;
    struct sway_switch *switch_device;
    struct sway_tablet *tablet;
    struct sway_tablet_pad *tablet_pad;
    struct wl_list link; // sway_seat::devices
};

struct sway_seat_node {
    struct sway_seat *seat;
    struct wls_transaction_node *node;

    struct wl_list link; // sway_seat::focus_stack

    struct wl_listener destroy;
};

struct sway_drag_icon {
    struct sway_seat *seat;
    struct wlr_drag_icon *wlr_drag_icon;
    struct wl_list link; // sway_root::drag_icons

    double x, y; // in layout-local coordinates

    struct wl_listener surface_commit;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
};

struct sway_drag {
    struct sway_seat *seat;
    struct wlr_drag *wlr_drag;
    struct wl_listener destroy;
};

struct sway_seat {
    struct wlr_seat *wlr_seat;
    struct sway_cursor *cursor;

    bool has_focus;
    struct wl_list focus_stack; // list of windows in focus order

    // If the focused layer is set, views cannot receive keyboard focus
    struct wlr_layer_surface_v1 *focused_layer;

    // If exclusive_client is set, no other clients will receive input events
    struct wl_client *exclusive_client;

    // Last touch point
    int32_t touch_id;
    double touch_x, touch_y;

    // whether a device that functions as cursor is currently being pressed
    bool cursor_pressed;

    // Seat operations (drag and resize)
    const struct sway_seatop_impl *seatop_impl;
    void *seatop_data;

    uint32_t last_button_serial;

    uint32_t idle_inhibit_sources, idle_wake_sources;

    struct sway_input_method_relay im_relay;

    struct wl_listener focus_destroy;
    struct wl_listener new_node;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;

    struct wl_list devices; // sway_seat_device::link
    struct wl_list keyboard_groups; // sway_keyboard_group::link
    struct wl_list keyboard_shortcuts_inhibitors;
                // sway_keyboard_shortcuts_inhibitor::link

    struct wl_list link; // input_manager::seats
};

struct sway_pointer_constraint {
    struct sway_cursor *cursor;
    struct wlr_pointer_constraint_v1 *constraint;

    struct wl_listener set_region;
    struct wl_listener destroy;
};

struct sway_keyboard_shortcuts_inhibitor {
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;

    struct wl_listener destroy;

    struct wl_list link; // sway_seat::keyboard_shortcuts_inhibitors
};

struct sway_seat *seat_create(const char *seat_name);

void seat_destroy(struct sway_seat *seat);

void seat_add_device(struct sway_seat *seat,
        struct sway_input_device *device);

void seat_configure_device(struct sway_seat *seat,
        struct sway_input_device *device);

void seat_reset_device(struct sway_seat *seat,
        struct sway_input_device *input_device);

void seat_remove_device(struct sway_seat *seat,
        struct sway_input_device *device);

void seat_configure_xcursor(struct sway_seat *seat);

void seat_set_focus(struct sway_seat *seat, struct wls_transaction_node *node);

void seat_set_focus_window(struct sway_seat *seat,
        struct wls_window *win);

void seat_set_focus_output(struct sway_seat *seat,
        struct sway_output *output);

/**
 * Manipulate the focus stack without triggering any other behaviour.
 *
 * This can be used to set focus_inactive by calling the function a second time
 * with the real focus.
 */
void seat_set_raw_focus(struct sway_seat *seat, struct wls_transaction_node *node);

void seat_set_focus_surface(struct sway_seat *seat,
        struct wlr_surface *surface, bool unfocus);

void seat_set_focus_layer(struct sway_seat *seat,
        struct wlr_layer_surface_v1 *layer);

void remove_node_from_focus_stack(struct sway_seat *seat,
        struct wls_transaction_node *node);

void seat_set_exclusive_client(struct sway_seat *seat,
        struct wl_client *client);

struct wls_transaction_node *seat_get_focus(struct sway_seat *seat);

struct sway_output *seat_get_focused_output(struct sway_seat *seat);

struct wls_window *seat_get_focused_window(struct sway_seat *seat);

struct wls_transaction_node *seat_get_next_in_focus_stack(struct sway_seat *seat);
/**
 * Return the last window to be focused for the seat (or the most recently
 * opened if no window has received focused) that is a child of the given
 * window. The focus-inactive window of the root window is the focused
 * window for the seat (if the seat does have focus). This function can be
 * used to determine what window gets focused next if the focused window
 * is destroyed, or focus moves to a window with children and we need to
 * descend into the next leaf in focus order.
 */
struct wls_transaction_node *seat_get_focus_inactive(struct sway_seat *seat,
        struct wls_transaction_node *node);

/**
 * Descend into the focus stack to find the focus-inactive view. Useful for
 * window placement when they change position in the tree.
 */
struct wls_window *seat_get_focus_inactive_view(struct sway_seat *seat,
        struct wls_transaction_node *ancestor);

/**
 * Return the immediate child of window which was most recently focused.
 */
struct wls_transaction_node *seat_get_active_tiling_child(struct sway_seat *seat,
        struct wls_transaction_node *parent);

/**
 * Iterate over the focus-inactive children of the window calling the
 * function on each.
 */
void seat_for_each_node(struct sway_seat *seat,
        void (*f)(struct wls_transaction_node *node, void *data), void *data);

void seat_apply_config(struct sway_seat *seat, struct seat_config *seat_config);

struct seat_config *seat_get_config(struct sway_seat *seat);

struct seat_config *seat_get_config_by_name(const char *name);

void seat_idle_notify_activity(struct sway_seat *seat,
        enum sway_input_idle_source source);

bool seat_is_input_allowed(struct sway_seat *seat, struct wlr_surface *surface);

void drag_icon_update_position(struct sway_drag_icon *icon);

void seatop_begin_default(struct sway_seat *seat);

void seatop_begin_down(struct sway_seat *seat, struct wls_window *win,
        uint32_t time_msec, int sx, int sy);

void seat_pointer_notify_button(struct sway_seat *seat, uint32_t time_msec,
        uint32_t button, enum wlr_button_state state);

void seat_consider_warp_to_focus(struct sway_seat *seat);

void seatop_button(struct sway_seat *seat, uint32_t time_msec,
        struct wlr_input_device *device, uint32_t button,
        enum wlr_button_state state);

void seatop_pointer_motion(struct sway_seat *seat, uint32_t time_msec);

void seatop_pointer_axis(struct sway_seat *seat,
        struct wlr_event_pointer_axis *event);

void seatop_tablet_tool_tip(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec,
        enum wlr_tablet_tool_tip_state state);

void seatop_tablet_tool_motion(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec);

void seatop_rebase(struct sway_seat *seat, uint32_t time_msec);

/**
 * End a seatop (ie. free any seatop specific resources).
 */
void seatop_end(struct sway_seat *seat);

/**
 * Instructs the seatop implementation to drop any references to the given
 * window (eg. because the window is destroying).
 * The seatop may choose to abort itself in response to this.
 */
void seatop_unref(struct sway_seat *seat, struct wls_window *win);

bool seatop_allows_set_cursor(struct sway_seat *seat);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the given surface
 * or NULL if none exists.
 */
struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(const struct sway_seat *seat,
        const struct wlr_surface *surface);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the currently
 * focused surface of a seat or NULL if none exists.
 */
struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(const struct sway_seat *seat);

#endif
