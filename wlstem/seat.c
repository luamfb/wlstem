#define _POSIX_C_SOURCE 200809L
#include <wayland-server-core.h>
#include "container.h"
#include "seat.h"

struct wls_transaction_node *seat_get_focus(struct sway_seat *seat) {
    if (!seat->has_focus) {
        return NULL;
    }
    if (wl_list_empty(&seat->focus_stack)) {
        return NULL;
    }
    struct sway_seat_node *current =
        wl_container_of(seat->focus_stack.next, current, link);
    return current->node;
}

struct sway_container *seat_get_focused_container(struct sway_seat *seat) {
    struct wls_transaction_node *focus = seat_get_focus(seat);
    if (focus && focus->type == N_CONTAINER) {
        return focus->sway_container;
    }
    return NULL;
}

void seatop_rebase(struct sway_seat *seat, uint32_t time_msec) {
    if (!seat->cursor_pressed) {
        seat->seatop_impl->rebase(seat, time_msec);
    }
}
