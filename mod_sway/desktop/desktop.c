#include "container.h"
#include "sway_desktop.h"
#include "output.h"
#include "view.h"
#include "wlstem.h"

void desktop_damage_surface(struct wlr_surface *surface, double lx, double ly,
        bool whole) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        struct wlr_box *output_box = wlr_output_layout_get_box(
            wls->output_manager->output_layout, output->wlr_output);
        output_damage_surface(output, lx - output_box->x,
            ly - output_box->y, surface, whole);
    }
}

void desktop_damage_whole_container(struct sway_container *con) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        output_damage_whole_container(output, con);
    }
}

void desktop_damage_box(struct wlr_box *box) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        output_damage_box(output, box);
    }
}

void desktop_damage_view(struct sway_view *view) {
    desktop_damage_whole_container(view->container);
    struct wlr_box box = {
        .x = view->container->current.content_x - view->geometry.x,
        .y = view->container->current.content_y - view->geometry.y,
        .width = view->surface->current.width,
        .height = view->surface->current.height,
    };
    desktop_damage_box(&box);
}
