#define _POSIX_C_SOURCE 200809L
#include <wayland-server-core.h>
#include "sway_transaction.h"
#include "input_manager.h"
#include "seat.h"
#include "sway_server.h"
#include "output.h"
#include "container.h"
#include "log.h"

struct sway_container *container_at(struct sway_output *output,
        double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    struct sway_container *c;

    struct sway_seat *seat = input_manager_current_seat();
    struct sway_container *focus = seat_get_focused_container(seat);
    if (!sway_assert(output->active, "Output is not active")) {
        return NULL;
    }

    // Focused view's popups
    if (focus && focus->view) {
        c = surface_at_view(focus, lx, ly, surface, sx, sy);
        if (c && surface_is_popup(*surface)) {
            return c;
        }
        *surface = NULL;
    }
    // Tiling (focused)
    if (focus && focus->view) {
        if ((c = surface_at_view(focus, lx, ly, surface, sx, sy))) {
            return c;
        }
    }
    // Tiling (non-focused)
    if ((c = tiling_container_at(&output->node, lx, ly, surface, sx, sy))) {
        return c;
    }
    return NULL;
}

void container_end_mouse_operation(struct sway_container *container) {
    struct sway_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        seatop_unref(seat, container);
    }
}
