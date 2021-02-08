#define _POSIX_C_SOURCE 200809L
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "log.h"

#define AXIS_HORIZONTAL (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
#define AXIS_VERTICAL   (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)

static bool is_horizontal(uint32_t axis) {
    return axis & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
}

struct seatop_resize_tiling_event {
	struct sway_container *con;    // leaf container

	// con, or ancestor of con which will be resized horizontally/vertically
	struct sway_container *h_con;
	struct sway_container *v_con;

	// sibling con(s) that will be resized to accommodate
	struct sway_container *h_sib;
	struct sway_container *v_sib;

	enum wlr_edges edge;
	enum wlr_edges edge_x, edge_y;
	double ref_lx, ref_ly;         // cursor's x/y at start of op
	double h_con_orig_width;       // width of the horizontal ancestor at start
	double v_con_orig_height;      // height of the vertical ancestor at start
};

static struct sway_container *container_find_resize_parent(struct sway_container *con,
		uint32_t axis) {
	enum sway_container_layout parallel_layout = L_HORIZ;
	bool allow_first = axis != WLR_EDGE_TOP && axis != WLR_EDGE_LEFT;
	bool allow_last = axis != WLR_EDGE_RIGHT && axis != WLR_EDGE_BOTTOM;

	while (con) {
		list_t *siblings = container_get_siblings(con);
		int index = container_sibling_index(con);
		if (container_parent_layout(con) == parallel_layout &&
				siblings->length > 1 && (allow_first || index > 0) &&
				(allow_last || index < siblings->length - 1)) {
			return con;
		}
		con = con->parent;
	}

	return NULL;
}

static void container_resize_tiled(struct sway_container *con,
		uint32_t axis, int amount) {
	if (!con) {
		return;
	}

	con = container_find_resize_parent(con, axis);
	if (!con) {
		// Can't resize in this direction
		return;
	}

	// For HORIZONTAL or VERTICAL, we are growing in two directions so select
	// both adjacent siblings. For RIGHT or DOWN, just select the next sibling.
	// For LEFT or UP, convert it to a RIGHT or DOWN resize and reassign con to
	// the previous sibling.
	struct sway_container *prev = NULL;
	struct sway_container *next = NULL;
	list_t *siblings = container_get_siblings(con);
	int index = container_sibling_index(con);

	if (axis == AXIS_HORIZONTAL || axis == AXIS_VERTICAL) {
		if (index == 0) {
			next = siblings->items[1];
		} else if (index == siblings->length - 1) {
			// Convert edge to top/left
			next = con;
			con = siblings->items[index - 1];
			amount = -amount;
		} else {
			prev = siblings->items[index - 1];
			next = siblings->items[index + 1];
		}
	} else if (axis == WLR_EDGE_TOP || axis == WLR_EDGE_LEFT) {
		if (!sway_assert(index > 0, "Didn't expect first child")) {
			return;
		}
		next = con;
		con = siblings->items[index - 1];
		amount = -amount;
	} else {
		if (!sway_assert(index < siblings->length - 1,
					"Didn't expect last child")) {
			return;
		}
		next = siblings->items[index + 1];
	}

	// Apply new dimensions
	int sibling_amount = prev ? ceil((double)amount / 2.0) : amount;

	if (is_horizontal(axis)) {
		if (con->width + amount < MIN_SANE_W) {
			return;
		}
		if (next->width - sibling_amount < MIN_SANE_W) {
			return;
		}
		if (prev && prev->width - sibling_amount < MIN_SANE_W) {
			return;
		}
		if (con->child_total_width <= 0) {
			return;
		}

		// We're going to resize so snap all the width fractions to full pixels
		// to avoid rounding issues
		list_t *siblings = container_get_siblings(con);
		for (int i = 0; i < siblings->length; ++i) {
			struct sway_container *con = siblings->items[i];
			con->width_fraction = con->width / con->child_total_width;
		}

		double amount_fraction = (double)amount / con->child_total_width;
		double sibling_amount_fraction =
			prev ? amount_fraction / 2.0 : amount_fraction;

		con->width_fraction += amount_fraction;
		next->width_fraction -= sibling_amount_fraction;
		if (prev) {
			prev->width_fraction -= sibling_amount_fraction;
		}
	} else {
		if (con->height + amount < MIN_SANE_H) {
			return;
		}
		if (next->height - sibling_amount < MIN_SANE_H) {
			return;
		}
		if (prev && prev->height - sibling_amount < MIN_SANE_H) {
			return;
		}
		if (con->child_total_height <= 0) {
			return;
		}

		// We're going to resize so snap all the height fractions to full pixels
		// to avoid rounding issues
		list_t *siblings = container_get_siblings(con);
		for (int i = 0; i < siblings->length; ++i) {
			struct sway_container *con = siblings->items[i];
			con->height_fraction = con->height / con->child_total_height;
		}

		double amount_fraction = (double)amount / con->child_total_height;
		double sibling_amount_fraction =
			prev ? amount_fraction / 2.0 : amount_fraction;

		con->height_fraction += amount_fraction;
		next->height_fraction -= sibling_amount_fraction;
		if (prev) {
			prev->height_fraction -= sibling_amount_fraction;
		}
	}

	if (con->parent) {
		arrange_container(con->parent);
	} else {
		arrange_workspace(con->workspace);
	}
}


static struct sway_container *container_get_resize_sibling(
		struct sway_container *con, uint32_t edge) {
	if (!con) {
		return NULL;
	}

	list_t *siblings = container_get_siblings(con);
	int index = container_sibling_index(con);
	int offset = edge & (WLR_EDGE_TOP | WLR_EDGE_LEFT) ? -1 : 1;

	if (siblings->length == 1) {
		return NULL;
	} else {
		return siblings->items[index + offset];
	}
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;

	if (seat->cursor->pressed_button_count == 0) {
		if (e->h_con) {
			container_set_resizing(e->h_con, false);
			container_set_resizing(e->h_sib, false);
			if (e->h_con->parent) {
				arrange_container(e->h_con->parent);
			} else {
				arrange_workspace(e->h_con->workspace);
			}
		}
		if (e->v_con) {
			container_set_resizing(e->v_con, false);
			container_set_resizing(e->v_sib, false);
			if (e->v_con->parent) {
				arrange_container(e->v_con->parent);
			} else {
				arrange_workspace(e->v_con->workspace);
			}
		}
		seatop_begin_default(seat);
	}
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;
	int amount_x = 0;
	int amount_y = 0;
	int moved_x = seat->cursor->cursor->x - e->ref_lx;
	int moved_y = seat->cursor->cursor->y - e->ref_ly;

	if (e->h_con) {
		if (e->edge & WLR_EDGE_LEFT) {
			amount_x = (e->h_con_orig_width - moved_x) - e->h_con->width;
		} else if (e->edge & WLR_EDGE_RIGHT) {
			amount_x = (e->h_con_orig_width + moved_x) - e->h_con->width;
		}
	}
	if (e->v_con) {
		if (e->edge & WLR_EDGE_TOP) {
			amount_y = (e->v_con_orig_height - moved_y) - e->v_con->height;
		} else if (e->edge & WLR_EDGE_BOTTOM) {
			amount_y = (e->v_con_orig_height + moved_y) - e->v_con->height;
		}
	}

	if (amount_x != 0) {
		container_resize_tiled(e->h_con, e->edge_x, amount_x);
	}
	if (amount_y != 0) {
		container_resize_tiled(e->v_con, e->edge_y, amount_y);
	}
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;
	if (e->con == con) {
		seatop_begin_default(seat);
	}
	if (e->h_sib == con || e->v_sib == con) {
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.unref = handle_unref,
};

void seatop_begin_resize_tiling(struct sway_seat *seat,
		struct sway_container *con, enum wlr_edges edge) {
	seatop_end(seat);

	struct seatop_resize_tiling_event *e =
		calloc(1, sizeof(struct seatop_resize_tiling_event));
	if (!e) {
		return;
	}
	e->con = con;
	e->edge = edge;

	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;

	if (edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) {
		e->edge_x = edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
		e->h_con = container_find_resize_parent(e->con, e->edge_x);
		e->h_sib = container_get_resize_sibling(e->h_con, e->edge_x);

		if (e->h_con) {
			container_set_resizing(e->h_con, true);
			container_set_resizing(e->h_sib, true);
			e->h_con_orig_width = e->h_con->width;
		}
	}
	if (edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) {
		e->edge_y = edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
		e->v_con = container_find_resize_parent(e->con, e->edge_y);
		e->v_sib = container_get_resize_sibling(e->v_con, e->edge_y);

		if (e->v_con) {
			container_set_resizing(e->v_con, true);
			container_set_resizing(e->v_sib, true);
			e->v_con_orig_height = e->v_con->height;
		}
	}

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
