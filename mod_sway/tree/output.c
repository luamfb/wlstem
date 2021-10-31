#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_damage.h>
#include "foreach.h"
#include "output.h"
#include "seat.h"
#include "sway_server.h"
#include "log.h"
#include "util.h"
#include "wlstem.h"

void seize_containers_from_noop_output(struct sway_output *output) {
    if (wls->output_manager->noop_output->active) {
        output_seize_containers_from(output, wls->output_manager->noop_output);
    }
}

struct sway_output * choose_absorber_output(struct sway_output *giver) {
    struct sway_output *absorber = NULL;
    if (wls->output_manager->outputs->length > 1) {
        absorber = wls->output_manager->outputs->items[0];
        if (absorber == giver) {
            absorber = wls->output_manager->outputs->items[1];
        }
    }
    return absorber;
}

struct sway_output *output_from_wlr_output(struct wlr_output *output) {
    return output->data;
}

struct sway_output *output_get_in_direction(struct sway_output *reference,
        enum wlr_direction direction) {
    if (!sway_assert(direction, "got invalid direction: %d", direction)) {
        return NULL;
    }
    struct wlr_box *output_box =
        wlr_output_layout_get_box(wls->output_manager->output_layout, reference->wlr_output);
    int lx = output_box->x + output_box->width / 2;
    int ly = output_box->y + output_box->height / 2;
    struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
            wls->output_manager->output_layout, direction, reference->wlr_output, lx, ly);
    if (!wlr_adjacent) {
        return NULL;
    }
    return output_from_wlr_output(wlr_adjacent);
}

void output_get_render_box(struct sway_output *output, struct wlr_box *box) {
    box->x = output->render_lx;
    box->y = output->render_ly;
    box->width = output->usable_area.width;
    box->height = output->usable_area.height;
}
