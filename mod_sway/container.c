#define _POSIX_C_SOURCE 200809L
#include <wayland-server-core.h>
#include "transaction.h"
#include "input_manager.h"
#include "seat.h"
#include "sway_server.h"
#include "output.h"
#include "log.h"
#include "window.h"
#include "wlstem.h"

struct wls_window *window_at(struct sway_output *output,
        double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    struct wls_window *win;

    struct sway_seat *seat = input_manager_current_seat();
    struct wls_window *focus = seat_get_focused_window(seat);
    if (!sway_assert(output->active, "Output is not active")) {
        return NULL;
    }

    // Focused view's popups
    if (focus && focus->view) {
        win = surface_at_view(focus, lx, ly, surface, sx, sy);
        if (win && surface_is_popup(*surface)) {
            return win;
        }
        *surface = NULL;
    }
    // Toplevel window (focused)
    if (focus && focus->view) {
        if ((win = surface_at_view(focus, lx, ly, surface, sx, sy))) {
            return win;
        }
    }
    // Toplevel window (non-focused)
    if ((win = toplevel_window_at(&output->node, lx, ly, surface, sx, sy))) {
        return win;
    }
    return NULL;
}

void window_begin_destroy(struct wls_window *win) {
    wl_signal_emit(&win->node.events.destroy, &win->node);

    window_end_mouse_operation(win);

    win->node.destroying = true;
    node_set_dirty(&win->node);

    if (win->output) {
        window_detach(win);
    }
}

void window_end_mouse_operation(struct wls_window *window) {
    struct sway_seat *seat;
    wl_list_for_each(seat, &wls->seats, link) {
        seatop_unref(seat, window);
    }
}
