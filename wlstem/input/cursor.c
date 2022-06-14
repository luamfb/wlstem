#include "cursor.h"
#include "seat.h"
#include "util.h"
#include "wlstem.h"

void cursor_rebase(struct sway_cursor *cursor) {
    uint32_t time_msec = get_current_time_msec();
    seatop_rebase(cursor->seat, time_msec);
}

void cursor_rebase_all(void) {
    if (!wls->output_manager->outputs->length) {
        return;
    }

    struct sway_seat *seat;
    wl_list_for_each(seat, &wls->seats, link) {
        cursor_rebase(seat->cursor);
    }
}
