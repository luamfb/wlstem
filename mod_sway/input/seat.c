#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <linux/input-event-codes.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "config.h"
#include "cursor.h"
#include "damage.h"
#include "foreach.h"
#include "list.h"
#include "log.h"
#include "sway_config.h"
#include "input_manager.h"
#include "sway_keyboard.h"
#include "seat.h"
#include "sway_switch.h"
#include "tablet.h"
#include "layers.h"
#include "output.h"
#include "sway_server.h"
#include "server_wm.h"
#include "server_arrange.h"
#include "window.h"
#include "output_manager.h"
#include "view.h"
#include "wlstem.h"
#include "server.h"

static void seat_device_destroy(struct sway_seat_device *seat_device) {
    if (!seat_device) {
        return;
    }

    sway_keyboard_destroy(seat_device->keyboard);
    sway_tablet_destroy(seat_device->tablet);
    sway_tablet_pad_destroy(seat_device->tablet_pad);
    wlr_cursor_detach_input_device(seat_device->sway_seat->cursor->cursor,
        seat_device->input_device->wlr_device);
    wl_list_remove(&seat_device->link);
    free(seat_device);
}

static void seat_node_destroy(struct sway_seat_node *seat_node) {
    wl_list_remove(&seat_node->destroy.link);
    wl_list_remove(&seat_node->link);
    free(seat_node);
}

void seat_destroy(struct sway_seat *seat) {
    if (seat == wls->current_seat) {
        wls->current_seat = input_manager_get_default_seat();
    }
    struct sway_seat_device *seat_device, *next;
    wl_list_for_each_safe(seat_device, next, &seat->devices, link) {
        seat_device_destroy(seat_device);
    }
    struct sway_seat_node *seat_node, *next_seat_node;
    wl_list_for_each_safe(seat_node, next_seat_node, &seat->focus_stack,
            link) {
        seat_node_destroy(seat_node);
    }
    sway_input_method_relay_finish(&seat->im_relay);
    sway_cursor_destroy(seat->cursor);
    wl_list_remove(&seat->new_node.link);
    wl_list_remove(&seat->request_start_drag.link);
    wl_list_remove(&seat->start_drag.link);
    wl_list_remove(&seat->request_set_selection.link);
    wl_list_remove(&seat->request_set_primary_selection.link);
    wl_list_remove(&seat->link);
    wlr_seat_destroy(seat->wlr_seat);
    free(seat);
}

void seat_idle_notify_activity(struct sway_seat *seat,
        enum sway_input_idle_source source) {
    uint32_t mask = seat->idle_inhibit_sources;
    struct wlr_idle_timeout *timeout;
    int ntimers = 0, nidle = 0;
    wl_list_for_each(timeout, &wls->misc_protocols->idle->idle_timers, link) {
        ++ntimers;
        if (timeout->idle_state) {
            ++nidle;
        }
    }
    if (nidle == ntimers) {
        mask = seat->idle_wake_sources;
    }
    if ((source & mask) > 0) {
        wlr_idle_notify_activity(wls->misc_protocols->idle, seat->wlr_seat);
    }
}

/**
 * Activate all views within this window recursively.
 */
static void seat_send_activate(struct wls_transaction_node *node, struct sway_seat *seat) {
    if (node_is_view(node)) {
        if (!seat_is_input_allowed(seat, node->wls_window->view->surface)) {
            sway_log(SWAY_DEBUG, "Refusing to set focus, input is inhibited");
            return;
        }
        view_set_activated(node->wls_window->view, true);
    } else {
        list_t *children = node_get_children(node);
        if (children) {
            for (int i = 0; i < children->length; ++i) {
                struct wls_window *child = children->items[i];
                seat_send_activate(&child->node, seat);
            }
        }
    }
}

static struct sway_keyboard *sway_keyboard_for_wlr_keyboard(
        struct sway_seat *seat, struct wlr_keyboard *wlr_keyboard) {
    struct sway_seat_device *seat_device;
    wl_list_for_each(seat_device, &seat->devices, link) {
        struct sway_input_device *input_device = seat_device->input_device;
        if (input_device->wlr_device->type != WLR_INPUT_DEVICE_KEYBOARD) {
            continue;
        }
        if (input_device->wlr_device->keyboard == wlr_keyboard) {
            return seat_device->keyboard;
        }
    }
    struct sway_keyboard_group *group;
    wl_list_for_each(group, &seat->keyboard_groups, link) {
        struct sway_input_device *input_device =
            group->seat_device->input_device;
        if (input_device->wlr_device->keyboard == wlr_keyboard) {
            return group->seat_device->keyboard;
        }
    }
    return NULL;
}

static void seat_keyboard_notify_enter(struct sway_seat *seat,
        struct wlr_surface *surface) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
    if (!keyboard) {
        wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface, NULL, 0, NULL);
        return;
    }

    struct sway_keyboard *sway_keyboard =
        sway_keyboard_for_wlr_keyboard(seat, keyboard);
    assert(sway_keyboard && "Cannot find sway_keyboard for seat keyboard");

    struct sway_shortcut_state *state = &sway_keyboard->state_pressed_sent;
    wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface,
            state->pressed_keycodes, state->npressed, &keyboard->modifiers);
}

static void seat_tablet_pads_notify_enter(struct sway_seat *seat,
        struct wlr_surface *surface) {
    struct sway_seat_device *seat_device;
    wl_list_for_each(seat_device, &seat->devices, link) {
        sway_tablet_pad_notify_enter(seat_device->tablet_pad, surface);
    }
}

/**
 * If win is a view, set it as active and enable keyboard input.
 * If win is a window, set all child views as active and don't enable
 * keyboard input on any.
 */
static void seat_send_focus(struct wls_transaction_node *node, struct sway_seat *seat) {
    seat_send_activate(node, seat);

    struct sway_view *view = node->type == N_WINDOW ?
        node->wls_window->view : NULL;

    if (view && seat_is_input_allowed(seat, view->surface)) {
#if HAVE_XWAYLAND
        if (view->type == SWAY_VIEW_XWAYLAND) {
            struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
            wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
        }
#endif

        seat_keyboard_notify_enter(seat, view->surface);
        seat_tablet_pads_notify_enter(seat, view->surface);
        sway_input_method_relay_set_focus(&seat->im_relay, view->surface);

        struct wlr_pointer_constraint_v1 *constraint =
            wlr_pointer_constraints_v1_constraint_for_surface(
                server.pointer_constraints, view->surface, seat->wlr_seat);
        sway_cursor_constrain(seat->cursor, constraint);
    }
}

void seat_for_each_node(struct sway_seat *seat,
        void (*f)(struct wls_transaction_node *node, void *data), void *data) {
    struct sway_seat_node *current = NULL;
    wl_list_for_each(current, &seat->focus_stack, link) {
        f(current->node, data);
    }
}

struct wls_window *seat_get_focus_inactive_view(struct sway_seat *seat,
        struct wls_transaction_node *ancestor) {
    if (ancestor->type == N_WINDOW && ancestor->wls_window->view) {
        return ancestor->wls_window;
    }
    struct sway_seat_node *current;
    wl_list_for_each(current, &seat->focus_stack, link) {
        struct wls_transaction_node *node = current->node;
        if (node->type == N_WINDOW && node->wls_window->view &&
                node_has_ancestor(node, ancestor)) {
            return node->wls_window;
        }
    }
    return NULL;
}

static void handle_seat_node_destroy(struct wl_listener *listener, void *data) {
    struct sway_seat_node *seat_node =
        wl_container_of(listener, seat_node, destroy);
    struct sway_seat *seat = seat_node->seat;
    struct wls_transaction_node *node = seat_node->node;
    struct wls_transaction_node *parent = node_get_parent(node);
    struct wls_transaction_node *focus = seat_get_focus(seat);

    if (node->type == N_OUTPUT) {
        seat_node_destroy(seat_node);
        return;
    }

    bool needs_new_focus = focus &&
        (focus == node || node_has_ancestor(focus, node));

    seat_node_destroy(seat_node);

    if (!parent && !needs_new_focus) {
        // Destroying a window that is no longer in the tree
        return;
    }

    // Find new focus_inactive (ie. sibling, or workspace if no siblings left)
    struct wls_transaction_node *next_focus = NULL;
    while (next_focus == NULL && parent != NULL) {
        struct wls_window *win =
            seat_get_focus_inactive_view(seat, parent);
        next_focus = win ? &win->node : NULL;

        if (next_focus == NULL && parent->type == N_OUTPUT) {
            next_focus = parent;
            break;
        }

        parent = node_get_parent(parent);
    }

    if (!next_focus) {
        struct sway_output *output = NULL;
        struct sway_seat_node *current;
        wl_list_for_each(current, &seat->focus_stack, link) {
            struct wls_transaction_node *node = current->node;
            if (node->type == N_WINDOW &&
                    node->wls_window->output->active) {
                output = node->wls_window->output;
                break;
            } else if (node->type == N_OUTPUT) {
                output = node->sway_output;
                break;
            }
        }
        if (!output) {
            return;
        }
        struct wls_window *win =
            seat_get_focus_inactive_view(seat, &output->node);
        next_focus = win ? &(win->node) : &(output->node);
    }

    if (needs_new_focus) {
        // Make sure the workspace IPC event gets sent
        if (node->type == N_WINDOW) {
            seat_set_focus(seat, NULL);
        }
        // The structure change might have caused it to move up to the top of
        // the focus stack without sending focus notifications to the view
        if (seat_get_focus(seat) == next_focus) {
            seat_send_focus(next_focus, seat);
        } else {
            seat_set_focus(seat, next_focus);
        }
    } else {
        focus = seat_get_next_in_focus_stack(seat);
        seat_set_raw_focus(seat, next_focus);
        if (focus->type == N_WINDOW && focus->wls_window->output->active) {
            seat_set_raw_focus(seat, &focus->wls_window->output->node);
        }
        seat_set_raw_focus(seat, focus);
    }
}

static struct sway_seat_node *seat_node_from_node(
        struct sway_seat *seat, struct wls_transaction_node *node) {

    struct sway_seat_node *seat_node = NULL;
    wl_list_for_each(seat_node, &seat->focus_stack, link) {
        if (seat_node->node == node) {
            sway_log(SWAY_DEBUG, "found seat_node %p for node ID %lu",
                seat_node, node->id);
            return seat_node;
        }
    }

    seat_node = calloc(1, sizeof(struct sway_seat_node));
    if (seat_node == NULL) {
        sway_log(SWAY_ERROR, "could not allocate seat node");
        return NULL;
    }

    sway_log(SWAY_DEBUG, "new seat_node %p created for node ID %lu", seat_node,
        node->id);

    seat_node->node = node;
    seat_node->seat = seat;
    wl_list_insert(seat->focus_stack.prev, &seat_node->link);
    wl_signal_add(&node->events.destroy, &seat_node->destroy);
    seat_node->destroy.notify = handle_seat_node_destroy;

    return seat_node;
}

static void handle_new_node(struct wl_listener *listener, void *data) {
    struct sway_seat *seat = wl_container_of(listener, seat, new_node);
    struct wls_transaction_node *node = data;
    //XXX not WM related: move back to wlstem later
    if (node->type == N_OUTPUT) {
        sway_log(SWAY_DEBUG, "new output nodel: configuring cursor");
        input_manager_configure_xcursor();
    }
    // XXX till here
    seat_node_from_node(seat, node);
}

static void drag_icon_damage_whole(struct sway_drag_icon *icon) {
    if (!icon->wlr_drag_icon->mapped) {
        return;
    }
    desktop_damage_surface(icon->wlr_drag_icon->surface, icon->x, icon->y, true);
}

void drag_icon_update_position(struct sway_drag_icon *icon) {
    drag_icon_damage_whole(icon);

    struct wlr_drag_icon *wlr_icon = icon->wlr_drag_icon;
    struct sway_seat *seat = icon->seat;
    struct wlr_cursor *cursor = seat->cursor->cursor;
    switch (wlr_icon->drag->grab_type) {
    case WLR_DRAG_GRAB_KEYBOARD:
        return;
    case WLR_DRAG_GRAB_KEYBOARD_POINTER:
        icon->x = cursor->x;
        icon->y = cursor->y;
        break;
    case WLR_DRAG_GRAB_KEYBOARD_TOUCH:;
        struct wlr_touch_point *point =
            wlr_seat_touch_get_point(seat->wlr_seat, wlr_icon->drag->touch_id);
        if (point == NULL) {
            return;
        }
        icon->x = seat->touch_x;
        icon->y = seat->touch_y;
    }

    drag_icon_damage_whole(icon);
}

static void handle_request_start_drag(struct wl_listener *listener,
        void *data) {
}

static void handle_start_drag(struct wl_listener *listener, void *data) {
}

static void handle_request_set_selection(struct wl_listener *listener,
        void *data) {
    struct sway_seat *seat =
        wl_container_of(listener, seat, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(seat->wlr_seat, event->source, event->serial);
}

static void handle_request_set_primary_selection(struct wl_listener *listener,
        void *data) {
    struct sway_seat *seat =
        wl_container_of(listener, seat, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(seat->wlr_seat, event->source, event->serial);
}

static void collect_focus_iter(struct wls_transaction_node *node, void *data) {
    struct sway_seat *seat = data;
    struct sway_seat_node *seat_node = seat_node_from_node(seat, node);
    if (!seat_node) {
        return;
    }
    wl_list_remove(&seat_node->link);
    wl_list_insert(&seat->focus_stack, &seat_node->link);
}

static void collect_focus_output_iter(struct sway_output *output,
        void *data) {
    collect_focus_iter(&output->node, data);
}

static void collect_focus_window_iter(struct wls_window *window,
        void *data) {
    collect_focus_iter(&window->node, data);
}

struct sway_seat *seat_create(const char *seat_name) {
    struct sway_seat *seat = calloc(1, sizeof(struct sway_seat));
    if (!seat) {
        return NULL;
    }

    seat->wlr_seat = wlr_seat_create(wls->server->wl_display, seat_name);
    if (!sway_assert(seat->wlr_seat, "could not allocate seat")) {
        free(seat);
        return NULL;
    }
    seat->wlr_seat->data = seat;

    seat->cursor = sway_cursor_create(seat);
    if (!seat->cursor) {
        wlr_seat_destroy(seat->wlr_seat);
        free(seat);
        return NULL;
    }

    seat->idle_inhibit_sources = seat->idle_wake_sources =
        IDLE_SOURCE_KEYBOARD |
        IDLE_SOURCE_POINTER |
        IDLE_SOURCE_TOUCH |
        IDLE_SOURCE_TABLET_PAD |
        IDLE_SOURCE_TABLET_TOOL |
        IDLE_SOURCE_SWITCH;

    // init the focus stack
    wl_list_init(&seat->focus_stack);

    wl_list_init(&seat->devices);

    wls_output_layout_for_each_output(collect_focus_output_iter, seat);
    wls_output_layout_for_each_window(collect_focus_window_iter, seat);

    wl_signal_add(&wls->node_manager->events.new_node, &seat->new_node);
    seat->new_node.notify = handle_new_node;

    wl_signal_add(&seat->wlr_seat->events.request_start_drag,
        &seat->request_start_drag);
    seat->request_start_drag.notify = handle_request_start_drag;

    wl_signal_add(&seat->wlr_seat->events.start_drag, &seat->start_drag);
    seat->start_drag.notify = handle_start_drag;

    wl_signal_add(&seat->wlr_seat->events.request_set_selection,
        &seat->request_set_selection);
    seat->request_set_selection.notify = handle_request_set_selection;

    wl_signal_add(&seat->wlr_seat->events.request_set_primary_selection,
        &seat->request_set_primary_selection);
    seat->request_set_primary_selection.notify =
        handle_request_set_primary_selection;

    wl_list_init(&seat->keyboard_groups);
    wl_list_init(&seat->keyboard_shortcuts_inhibitors);

    sway_input_method_relay_init(seat, &seat->im_relay);

    bool first = wl_list_empty(&wls->seats);
    wl_list_insert(&wls->seats, &seat->link);

    if (!first) {
        // Since this is not the first seat, attempt to set initial focus
        struct sway_seat *current_seat = input_manager_current_seat();
        struct wls_transaction_node *current_focus =
            seat_get_next_in_focus_stack(current_seat);
        seat_set_focus(seat, current_focus);
    }

    seatop_begin_default(seat);

    return seat;
}

static void seat_update_capabilities(struct sway_seat *seat) {
    uint32_t caps = 0;
    uint32_t previous_caps = seat->wlr_seat->capabilities;
    struct sway_seat_device *seat_device;
    wl_list_for_each(seat_device, &seat->devices, link) {
        switch (seat_device->input_device->wlr_device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            caps |= WL_SEAT_CAPABILITY_KEYBOARD;
            break;
        case WLR_INPUT_DEVICE_POINTER:
            caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            caps |= WL_SEAT_CAPABILITY_TOUCH;
            break;
        case WLR_INPUT_DEVICE_TABLET_TOOL:
            caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case WLR_INPUT_DEVICE_SWITCH:
        case WLR_INPUT_DEVICE_TABLET_PAD:
            break;
        }
    }

    // Hide cursor if seat doesn't have pointer capability.
    // We must call cursor_set_image while the wlr_seat has the capabilities
    // otherwise it's a no op.
    if ((caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
        cursor_set_image(seat->cursor, NULL, NULL);
        wlr_seat_set_capabilities(seat->wlr_seat, caps);
    } else {
        wlr_seat_set_capabilities(seat->wlr_seat, caps);
        if ((previous_caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
            cursor_set_image(seat->cursor, "left_ptr", NULL);
        }
    }
}

static void seat_reset_input_config(struct sway_seat *seat,
        struct sway_seat_device *sway_device) {
    sway_log(SWAY_DEBUG, "Resetting output mapping for input device %s",
        sway_device->input_device->identifier);
    wlr_cursor_map_input_to_output(seat->cursor->cursor,
        sway_device->input_device->wlr_device, NULL);
}

static void seat_apply_input_config(struct sway_seat *seat,
        struct sway_seat_device *sway_device) {
    struct input_config *ic =
        input_device_get_config(sway_device->input_device);

    sway_log(SWAY_DEBUG, "Applying input config to %s",
        sway_device->input_device->identifier);

    const char *mapped_to_output = ic == NULL ? NULL : ic->mapped_to_output;
    struct wlr_box *mapped_to_region = ic == NULL ? NULL : ic->mapped_to_region;
    enum input_config_mapped_to mapped_to =
        ic == NULL ? MAPPED_TO_DEFAULT : ic->mapped_to;

    switch (mapped_to) {
    case MAPPED_TO_DEFAULT:
        mapped_to_output = sway_device->input_device->wlr_device->output_name;
        if (mapped_to_output == NULL) {
            return;
        }
        /* fallthrough */
    case MAPPED_TO_OUTPUT:
        sway_log(SWAY_DEBUG, "Mapping input device %s to output %s",
            sway_device->input_device->identifier, mapped_to_output);
        if (strcmp("*", mapped_to_output) == 0) {
            wlr_cursor_map_input_to_output(seat->cursor->cursor,
                sway_device->input_device->wlr_device, NULL);
            wlr_cursor_map_input_to_region(seat->cursor->cursor,
                sway_device->input_device->wlr_device, NULL);
            sway_log(SWAY_DEBUG, "Reset output mapping");
            return;
        }
        struct sway_output *output = output_by_name_or_id(mapped_to_output);
        if (!output) {
            sway_log(SWAY_DEBUG, "Requested output %s for device %s isn't present",
                mapped_to_output, sway_device->input_device->identifier);
            return;
        }
        wlr_cursor_map_input_to_output(seat->cursor->cursor,
            sway_device->input_device->wlr_device, output->wlr_output);
        wlr_cursor_map_input_to_region(seat->cursor->cursor,
            sway_device->input_device->wlr_device, NULL);
        sway_log(SWAY_DEBUG,
            "Mapped to output %s", output->wlr_output->name);
        return;
    case MAPPED_TO_REGION:
        sway_log(SWAY_DEBUG, "Mapping input device %s to %d,%d %dx%d",
            sway_device->input_device->identifier,
            mapped_to_region->x, mapped_to_region->y,
            mapped_to_region->width, mapped_to_region->height);
        wlr_cursor_map_input_to_output(seat->cursor->cursor,
            sway_device->input_device->wlr_device, NULL);
        wlr_cursor_map_input_to_region(seat->cursor->cursor,
            sway_device->input_device->wlr_device, mapped_to_region);
        return;
    }
}

static void seat_configure_pointer(struct sway_seat *seat,
        struct sway_seat_device *sway_device) {
    if ((seat->wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER) == 0) {
        seat_configure_xcursor(seat);
    }
    wlr_cursor_attach_input_device(seat->cursor->cursor,
        sway_device->input_device->wlr_device);
    seat_apply_input_config(seat, sway_device);
    wl_event_source_timer_update(
            seat->cursor->hide_source, cursor_get_timeout(seat->cursor));
}

static void seat_configure_keyboard(struct sway_seat *seat,
        struct sway_seat_device *seat_device) {
    if (!seat_device->keyboard) {
        sway_keyboard_create(seat, seat_device);
    }
    sway_keyboard_configure(seat_device->keyboard);
    wlr_seat_set_keyboard(seat->wlr_seat,
            seat_device->input_device->wlr_device);
    struct wls_transaction_node *focus = seat_get_focus(seat);
    if (focus && node_is_view(focus)) {
        // force notify reenter to pick up the new configuration
        wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
        seat_keyboard_notify_enter(seat, focus->wls_window->view->surface);
    }
}

static void seat_configure_switch(struct sway_seat *seat,
        struct sway_seat_device *seat_device) {
    if (!seat_device->switch_device) {
        sway_switch_create(seat, seat_device);
    }
    seat_apply_input_config(seat, seat_device);
    sway_switch_configure(seat_device->switch_device);
}

static void seat_configure_touch(struct sway_seat *seat,
        struct sway_seat_device *sway_device) {
    wlr_cursor_attach_input_device(seat->cursor->cursor,
        sway_device->input_device->wlr_device);
    seat_apply_input_config(seat, sway_device);
}

static void seat_configure_tablet_tool(struct sway_seat *seat,
        struct sway_seat_device *sway_device) {
    if (!sway_device->tablet) {
        sway_device->tablet = sway_tablet_create(seat, sway_device);
    }
    sway_configure_tablet(sway_device->tablet);
    wlr_cursor_attach_input_device(seat->cursor->cursor,
        sway_device->input_device->wlr_device);
    seat_apply_input_config(seat, sway_device);
}

static void seat_configure_tablet_pad(struct sway_seat *seat,
        struct sway_seat_device *sway_device) {
    if (!sway_device->tablet_pad) {
        sway_device->tablet_pad = sway_tablet_pad_create(seat, sway_device);
    }
    sway_configure_tablet_pad(sway_device->tablet_pad);
}

static struct sway_seat_device *seat_get_device(struct sway_seat *seat,
        struct sway_input_device *input_device) {
    struct sway_seat_device *seat_device = NULL;
    wl_list_for_each(seat_device, &seat->devices, link) {
        if (seat_device->input_device == input_device) {
            return seat_device;
        }
    }

    struct sway_keyboard_group *group = NULL;
    wl_list_for_each(group, &seat->keyboard_groups, link) {
        if (group->seat_device->input_device == input_device) {
            return group->seat_device;
        }
    }

    return NULL;
}

void seat_configure_device(struct sway_seat *seat,
        struct sway_input_device *input_device) {
    struct sway_seat_device *seat_device = seat_get_device(seat, input_device);
    if (!seat_device) {
        return;
    }

    switch (input_device->wlr_device->type) {
        case WLR_INPUT_DEVICE_POINTER:
            seat_configure_pointer(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_KEYBOARD:
            seat_configure_keyboard(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_SWITCH:
            seat_configure_switch(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            seat_configure_touch(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_TABLET_TOOL:
            seat_configure_tablet_tool(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_TABLET_PAD:
            seat_configure_tablet_pad(seat, seat_device);
            break;
    }
}

void seat_reset_device(struct sway_seat *seat,
        struct sway_input_device *input_device) {
    struct sway_seat_device *seat_device = seat_get_device(seat, input_device);
    if (!seat_device) {
        return;
    }

    switch (input_device->wlr_device->type) {
        case WLR_INPUT_DEVICE_POINTER:
            seat_reset_input_config(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_KEYBOARD:
            sway_keyboard_disarm_key_repeat(seat_device->keyboard);
            sway_keyboard_configure(seat_device->keyboard);
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            seat_reset_input_config(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_TABLET_TOOL:
            seat_reset_input_config(seat, seat_device);
            break;
        case WLR_INPUT_DEVICE_TABLET_PAD:
            sway_log(SWAY_DEBUG, "TODO: reset tablet pad");
            break;
        case WLR_INPUT_DEVICE_SWITCH:
            sway_log(SWAY_DEBUG, "TODO: reset switch device");
            break;
    }
}

void seat_add_device(struct sway_seat *seat,
        struct sway_input_device *input_device) {
    if (seat_get_device(seat, input_device)) {
        seat_configure_device(seat, input_device);
        return;
    }

    struct sway_seat_device *seat_device =
        calloc(1, sizeof(struct sway_seat_device));
    if (!seat_device) {
        sway_log(SWAY_DEBUG, "could not allocate seat device");
        return;
    }

    sway_log(SWAY_DEBUG, "adding device %s to seat %s",
        input_device->identifier, seat->wlr_seat->name);

    seat_device->sway_seat = seat;
    seat_device->input_device = input_device;
    wl_list_insert(&seat->devices, &seat_device->link);

    seat_configure_device(seat, input_device);

    seat_update_capabilities(seat);
}

void seat_remove_device(struct sway_seat *seat,
        struct sway_input_device *input_device) {
    struct sway_seat_device *seat_device = seat_get_device(seat, input_device);

    if (!seat_device) {
        return;
    }

    sway_log(SWAY_DEBUG, "removing device %s from seat %s",
        input_device->identifier, seat->wlr_seat->name);

    seat_device_destroy(seat_device);

    seat_update_capabilities(seat);
}

static bool xcursor_manager_is_named(const struct wlr_xcursor_manager *manager,
        const char *name) {
    return (!manager->name && !name) ||
        (name && manager->name && strcmp(name, manager->name) == 0);
}

void seat_configure_xcursor(struct sway_seat *seat) {
    unsigned cursor_size = 24;
    const char *cursor_theme = NULL;

    const struct seat_config *seat_config = seat_get_config(seat);
    if (!seat_config) {
        seat_config = seat_get_config_by_name("*");
    }
    if (seat_config) {
        cursor_size = seat_config->xcursor_theme.size;
        cursor_theme = seat_config->xcursor_theme.name;
    }

    if (seat == input_manager_get_default_seat()) {
        char cursor_size_fmt[16];
        snprintf(cursor_size_fmt, sizeof(cursor_size_fmt), "%u", cursor_size);
        setenv("XCURSOR_SIZE", cursor_size_fmt, 1);
        if (cursor_theme != NULL) {
            setenv("XCURSOR_THEME", cursor_theme, 1);
        }

#if HAVE_XWAYLAND
        if (server.xwayland.wlr_xwayland && (!server.xwayland.xcursor_manager ||
                !xcursor_manager_is_named(server.xwayland.xcursor_manager,
                    cursor_theme) ||
                server.xwayland.xcursor_manager->size != cursor_size)) {

            wlr_xcursor_manager_destroy(server.xwayland.xcursor_manager);

            server.xwayland.xcursor_manager =
                wlr_xcursor_manager_create(cursor_theme, cursor_size);
            sway_assert(server.xwayland.xcursor_manager,
                        "Cannot create XCursor manager for theme");

            wlr_xcursor_manager_load(server.xwayland.xcursor_manager, 1);
            struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
                server.xwayland.xcursor_manager, "left_ptr", 1);
            if (xcursor != NULL) {
                struct wlr_xcursor_image *image = xcursor->images[0];
                wlr_xwayland_set_cursor(
                    server.xwayland.wlr_xwayland, image->buffer,
                    image->width * 4, image->width, image->height,
                    image->hotspot_x, image->hotspot_y);
            }
        }
#endif
    }

    /* Create xcursor manager if we don't have one already, or if the
     * theme has changed */
    if (!seat->cursor->xcursor_manager ||
            !xcursor_manager_is_named(
                seat->cursor->xcursor_manager, cursor_theme) ||
            seat->cursor->xcursor_manager->size != cursor_size) {

        wlr_xcursor_manager_destroy(seat->cursor->xcursor_manager);
        seat->cursor->xcursor_manager =
            wlr_xcursor_manager_create(cursor_theme, cursor_size);
        if (!seat->cursor->xcursor_manager) {
            sway_log(SWAY_ERROR,
                "Cannot create XCursor manager for theme '%s'", cursor_theme);
        }
    }

    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *sway_output = wls->output_manager->outputs->items[i];
        struct wlr_output *output = sway_output->wlr_output;
        bool result =
            wlr_xcursor_manager_load(seat->cursor->xcursor_manager,
                output->scale);
        if (!result) {
            sway_log(SWAY_ERROR,
                "Cannot load xcursor theme for output '%s' with scale %f",
                output->name, output->scale);
        }
    }

    // Reset the cursor so that we apply it to outputs that just appeared
    cursor_set_image(seat->cursor, NULL, NULL);
    cursor_set_image(seat->cursor, "left_ptr", NULL);
    wlr_cursor_warp(seat->cursor->cursor, NULL, seat->cursor->cursor->x,
        seat->cursor->cursor->y);
}

bool seat_is_input_allowed(struct sway_seat *seat,
        struct wlr_surface *surface) {
    struct wl_client *client = wl_resource_get_client(surface->resource);
    return !seat->exclusive_client || seat->exclusive_client == client;
}

static void send_unfocus(struct wls_window *win, void *data) {
    if (win->view) {
        view_set_activated(win->view, false);
    }
}

// Unfocus the window and any children (eg. when leaving `focus parent`)
static void seat_send_unfocus(struct wls_transaction_node *node, struct sway_seat *seat) {
    sway_cursor_constrain(seat->cursor, NULL);
    wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
    if (node->type == N_OUTPUT) {
        output_for_each_window(node->sway_output, send_unfocus, seat);
    } else if (node->type == N_WINDOW) {
        send_unfocus(node->wls_window, seat);
    }
}

static int handle_urgent_timeout(void *data) {
    struct sway_view *view = data;
    view_set_urgent(view, false);
    return 0;
}

static void dump_focus_stack(struct sway_seat *seat) {
    sway_log(SWAY_DEBUG, "=== focus stack dump start ===");

    struct sway_seat_node *seat_node;
    wl_list_for_each(seat_node, &seat->focus_stack, link) {
        if (!seat_node) {
            sway_log(SWAY_ERROR, "NULL seat_node DETECTED IN FOCUS STACK!");
            continue;
        }
        sway_log(SWAY_DEBUG, "seat_node %p:", seat_node);
        struct wls_transaction_node *node = seat_node->node;
        if (!node) {
            sway_log(SWAY_ERROR, "NULL node DETECTED IN FOCUS STACK!");
            continue;
        }
        sway_log(SWAY_DEBUG, "   node ID %lu", node->id);
        if (node->type == N_WINDOW) {
            sway_log(SWAY_DEBUG, "   type: window (%p)", node->wls_window);
        } else if (node->type == N_OUTPUT) {
            sway_log(SWAY_DEBUG, "   type: output (%p)", node->sway_output);
        } else {
            sway_log(SWAY_ERROR, "   type: UNKNOWN!!");
        }

        sway_log(SWAY_DEBUG, "   referenced by %lu transactions", node->ntxnrefs);
        sway_log(SWAY_DEBUG, "   dirty = %s", node->dirty ? "true" : "false");
        sway_log(SWAY_DEBUG, "   destroying = %s", node->destroying ? "true" : "false");
    }

    sway_log(SWAY_DEBUG, "=== focus stack dump end ===");
}

void seat_set_raw_focus(struct sway_seat *seat, struct wls_transaction_node *node) {
    struct sway_seat_node *seat_node = seat_node_from_node(seat, node);
    assert(seat_node->node == node);
    sway_log(SWAY_DEBUG, "setting focus to seat_node %p with node ID %lu",
        seat_node, node->id);
    wl_list_remove(&seat_node->link);
    wl_list_insert(&seat->focus_stack, &seat_node->link);
    node_set_dirty(node);

    struct wls_transaction_node *parent = node_get_parent(node);
    node_set_dirty(parent);
    dump_focus_stack(seat);
}

void seat_set_focus(struct sway_seat *seat, struct wls_transaction_node *node) {
    if (seat->focused_layer) {
        struct wlr_layer_surface_v1 *layer = seat->focused_layer;
        seat_set_focus_layer(seat, NULL);
        seat_set_focus(seat, node);
        seat_set_focus_layer(seat, layer);
        return;
    }

    struct wls_transaction_node *last_focus = seat_get_focus(seat);
    if (last_focus == node) {
        return;
    }

    struct sway_output *last_output = seat_get_focused_output(seat);

    if (node == NULL) {
       if (last_focus) {
           // Close any popups on the old focus
           if (node_is_view(last_focus)) {
               view_close_popups(last_focus->wls_window->view);
           }
           seat_send_unfocus(last_focus, seat);
       }
       sway_input_method_relay_set_focus(&seat->im_relay, NULL);
       seat->has_focus = false;
       return;
    }

    struct sway_output *new_output = node->type == N_OUTPUT ?
        node->sway_output : node->wls_window->output;

    struct wls_window *window = node->type == N_WINDOW ?
        node->wls_window : NULL;


    if (new_output && last_output != new_output) {
        node_set_dirty(&new_output->node);
    }

    // Unfocus the previous focus
    if (last_focus) {
        seat_send_unfocus(last_focus, seat);
        node_set_dirty(last_focus);
        struct wls_transaction_node *parent = node_get_parent(last_focus);
        if (parent) {
            node_set_dirty(parent);
        }
    }

    if (new_output) {
        seat_set_raw_focus(seat, &new_output->node);
    }
    if (window) {
        seat_set_raw_focus(seat, &window->node);
        seat_send_focus(&window->node, seat);
    }

    // Close any popups on the old focus
    if (last_focus && node_is_view(last_focus)) {
        view_close_popups(last_focus->wls_window->view);
    }

    // If urgent, either unset the urgency or start a timer to unset it
    if (window && window->view && view_is_urgent(window->view) &&
            !window->view->urgent_timer) {
        struct sway_view *view = window->view;
        if (last_output && last_output != new_output &&
                config->urgent_timeout > 0) {
            view->urgent_timer = wl_event_loop_add_timer(wls->server->wl_event_loop,
                    handle_urgent_timeout, view);
            if (view->urgent_timer) {
                wl_event_source_timer_update(view->urgent_timer,
                        config->urgent_timeout);
            } else {
                sway_log_errno(SWAY_ERROR, "Unable to create urgency timer");
                handle_urgent_timeout(view);
            }
        } else {
            view_set_urgent(view, false);
        }
    }

    seat->has_focus = true;
}

void seat_set_focus_window(struct sway_seat *seat,
        struct wls_window *win) {
    seat_set_focus(seat, win ? &win->node : NULL);
}

void seat_set_focus_output(struct sway_seat *seat,
        struct sway_output *output) {
    seat_set_focus(seat, output ? &output->node : NULL);
}

void seat_set_focus_surface(struct sway_seat *seat,
        struct wlr_surface *surface, bool unfocus) {
    if (seat->has_focus && unfocus) {
        struct wls_transaction_node *focus = seat_get_focus(seat);
        seat_send_unfocus(focus, seat);
        seat->has_focus = false;
    }

    if (surface) {
        seat_keyboard_notify_enter(seat, surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
    }

    seat_tablet_pads_notify_enter(seat, surface);
}

void seat_set_focus_layer(struct sway_seat *seat,
        struct wlr_layer_surface_v1 *layer) {
    if (!layer && seat->focused_layer) {
        seat->focused_layer = NULL;
        struct wls_transaction_node *previous = seat_get_next_in_focus_stack(seat);
        if (previous) {
            // Hack to get seat to re-focus the return value of get_focus
            seat_set_focus(seat, NULL);
            seat_set_focus(seat, previous);
        }
        return;
    } else if (!layer || seat->focused_layer == layer) {
        return;
    }
    assert(layer->mapped);
    seat_set_focus_surface(seat, layer->surface, true);
    if (layer->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
        seat->focused_layer = layer;
    }
}

void seat_set_exclusive_client(struct sway_seat *seat,
        struct wl_client *client) {
    if (!client) {
        seat->exclusive_client = client;
        // Triggers a refocus of the topmost surface layer if necessary
        // TODO: Make layer surface focus per-output based on cursor position
        for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
            struct sway_output *output = wls->output_manager->outputs->items[i];
            arrange_layers(output);
        }
        return;
    }
    if (seat->focused_layer) {
        if (wl_resource_get_client(seat->focused_layer->resource) != client) {
            seat_set_focus_layer(seat, NULL);
        }
    }
    if (seat->has_focus) {
        struct wls_transaction_node *focus = seat_get_focus(seat);
        if (node_is_view(focus) && wl_resource_get_client(
                    focus->wls_window->view->surface->resource) != client) {
            seat_set_focus(seat, NULL);
        }
    }
    if (seat->wlr_seat->pointer_state.focused_client) {
        if (seat->wlr_seat->pointer_state.focused_client->client != client) {
            wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
        }
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct wlr_touch_point *point;
    wl_list_for_each(point, &seat->wlr_seat->touch_state.touch_points, link) {
        if (point->client->client != client) {
            wlr_seat_touch_point_clear_focus(seat->wlr_seat,
                    now.tv_nsec / 1000, point->touch_id);
        }
    }
    seat->exclusive_client = client;
}

struct wls_transaction_node *seat_get_next_in_focus_stack(struct sway_seat *seat) {
    dump_focus_stack(seat);
    if (wl_list_empty(&seat->focus_stack)) {
        sway_log(SWAY_DEBUG, "empty focus stack");
        return NULL;
    }
    struct sway_seat_node *seat_node = wl_container_of(seat->focus_stack.next, seat_node, link);
    if (!seat_node) {
        sway_log(SWAY_DEBUG, "seat_node is NULL!");
        return NULL;
    }
    struct wls_transaction_node *node = seat_node->node;
    sway_log(SWAY_DEBUG, "selected focused node ID %lu from seat_node %p",
        node->id, seat_node);
    return node;
}

struct wls_transaction_node *seat_get_focus_inactive(struct sway_seat *seat,
        struct wls_transaction_node *node) {
    if (node_is_view(node)) {
        return node;
    }
    struct sway_seat_node *current;
    wl_list_for_each(current, &seat->focus_stack, link) {
        if (node_has_ancestor(current->node, node)) {
            return current->node;
        }
    }
    if (node->type == N_OUTPUT) {
        return node;
    }
    return NULL;
}

struct wls_transaction_node *seat_get_active_tiling_child(struct sway_seat *seat,
        struct wls_transaction_node *parent) {
    if (node_is_view(parent)) {
        return parent;
    }
    struct sway_seat_node *current;
    wl_list_for_each(current, &seat->focus_stack, link) {
        struct wls_transaction_node *node = current->node;
        if (node_get_parent(node) != parent) {
            continue;
        }
        if (parent->type == N_OUTPUT) {
            struct sway_output *output = parent->sway_output;
            if (!output) {
                sway_log(SWAY_DEBUG, "workspace has no output!");
                continue;
            }
            if (list_find(output->windows, node->wls_window) == -1) {
                continue;
            }
        }
        return node;
    }
    return NULL;
}

struct sway_output *seat_get_focused_output(struct sway_seat *seat) {
    struct wls_transaction_node *focus = seat_get_next_in_focus_stack(seat);
    if (!focus) {
        return NULL;
    }
    if (focus->type == N_WINDOW) {
        return focus->wls_window->output;
    } else if (focus->type == N_OUTPUT) {
        return focus->sway_output;
    }
    return NULL;
}

void seat_apply_config(struct sway_seat *seat,
        struct seat_config *seat_config) {
    struct sway_seat_device *seat_device = NULL;

    if (!seat_config) {
        return;
    }

    seat->idle_inhibit_sources = seat_config->idle_inhibit_sources;
    seat->idle_wake_sources = seat_config->idle_wake_sources;

    wl_list_for_each(seat_device, &seat->devices, link) {
        seat_configure_device(seat, seat_device->input_device);
        cursor_handle_activity_from_device(seat->cursor,
            seat_device->input_device->wlr_device);
    }
}

struct seat_config *seat_get_config(struct sway_seat *seat) {
    struct seat_config *seat_config = NULL;
    for (int i = 0; i < config->seat_configs->length; ++i ) {
        seat_config = config->seat_configs->items[i];
        if (strcmp(seat->wlr_seat->name, seat_config->name) == 0) {
            return seat_config;
        }
    }

    return NULL;
}

struct seat_config *seat_get_config_by_name(const char *name) {
    struct seat_config *seat_config = NULL;
    for (int i = 0; i < config->seat_configs->length; ++i ) {
        seat_config = config->seat_configs->items[i];
        if (strcmp(name, seat_config->name) == 0) {
            return seat_config;
        }
    }

    return NULL;
}

void seat_pointer_notify_button(struct sway_seat *seat, uint32_t time_msec,
        uint32_t button, enum wlr_button_state state) {
    seat->last_button_serial = wlr_seat_pointer_notify_button(seat->wlr_seat,
            time_msec, button, state);
}

void seat_consider_warp_to_focus(struct sway_seat *seat) {
    struct wls_transaction_node *focus = seat_get_focus(seat);
    if (config->mouse_warping == WARP_NO || !focus) {
        return;
    }
    if (config->mouse_warping == WARP_OUTPUT) {
        struct sway_output *output = node_get_output(focus);
        if (output) {
            struct wlr_box box;
            output_get_box(output, &box);
            if (wlr_box_contains_point(&box,
                        seat->cursor->cursor->x, seat->cursor->cursor->y)) {
                return;
            }
        }
    }

    if (focus->type == N_WINDOW) {
        cursor_warp_to_window(seat->cursor, focus->wls_window, false);
    } else {
        cursor_warp_to_output(seat->cursor, focus->sway_output);
    }
}

void down_handle_unref(struct sway_seat *seat, struct wls_window *win);

void seatop_unref(struct sway_seat *seat, struct wls_window *win) {
    if (seat->cursor_pressed) {
        down_handle_unref(seat, win);
    }
}

void default_handle_button(struct sway_seat *seat, uint32_t time_msec,
        struct wlr_input_device *device, uint32_t button,
        enum wlr_button_state state);
void down_handle_button(struct sway_seat *seat, uint32_t time_msec,
        struct wlr_input_device *device, uint32_t button,
        enum wlr_button_state state);

void seatop_button(struct sway_seat *seat, uint32_t time_msec,
        struct wlr_input_device *device, uint32_t button,
        enum wlr_button_state state) {
    if (!seat->cursor_pressed) {
        default_handle_button(seat, time_msec, device, button, state);
    } else {
        down_handle_button(seat, time_msec, device, button, state);
    }
}


void default_handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec);
void down_handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec);

void seatop_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
    if (!seat->cursor_pressed) {
        default_handle_pointer_motion(seat, time_msec);
    } else {
        down_handle_pointer_motion(seat, time_msec);
    }
}

void default_handle_pointer_axis(struct sway_seat *seat,
        struct wlr_event_pointer_axis *event);
void down_handle_pointer_axis(struct sway_seat *seat,
        struct wlr_event_pointer_axis *event);

void seatop_pointer_axis(struct sway_seat *seat,
        struct wlr_event_pointer_axis *event) {
    if (!seat->cursor_pressed) {
        default_handle_pointer_axis(seat, event);
    } else {
        down_handle_pointer_axis(seat, event);
    }
}

void default_handle_tablet_tool_tip(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec,
        enum wlr_tablet_tool_tip_state state);
void down_handle_tablet_tool_tip(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec,
        enum wlr_tablet_tool_tip_state state);

void seatop_tablet_tool_tip(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec,
        enum wlr_tablet_tool_tip_state state) {
    if (!seat->cursor_pressed) {
        default_handle_tablet_tool_tip(seat, tool, time_msec, state);
    } else {
        down_handle_tablet_tool_tip(seat, tool, time_msec, state);
    }
}

void default_handle_tablet_tool_motion(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec);
void down_handle_tablet_tool_motion(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec);

void seatop_tablet_tool_motion(struct sway_seat *seat,
        struct sway_tablet_tool *tool, uint32_t time_msec) {
    if (!seat->cursor_pressed) {
        default_handle_tablet_tool_motion(seat, tool, time_msec);
    } else {
        down_handle_tablet_tool_motion(seat, tool, time_msec);
    }
}

void seatop_end(struct sway_seat *seat) {
    free(seat->seatop_data);
    seat->seatop_data = NULL;
    seat->seatop_impl = NULL;
}

bool seatop_allows_set_cursor(struct sway_seat *seat) {
    return true;
}

struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(
        const struct sway_seat *seat,
        const struct wlr_surface *surface) {
    struct sway_keyboard_shortcuts_inhibitor *sway_inhibitor = NULL;
    wl_list_for_each(sway_inhibitor, &seat->keyboard_shortcuts_inhibitors, link) {
        if (sway_inhibitor->inhibitor->surface == surface) {
            return sway_inhibitor;
        }
    }

    return NULL;
}

struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(
        const struct sway_seat *seat) {
    return keyboard_shortcuts_inhibitor_get_for_surface(seat,
        seat->wlr_seat->keyboard_state.focused_surface);
}
