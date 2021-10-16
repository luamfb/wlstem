#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/util/region.h>
#include "container.h"
#include "damage.h"
#include "foreach.h"
#include "output.h"
#include "view.h"
#include "wlstem.h"

static void damage_surface_iterator(struct sway_output *output, struct sway_view *view,
        struct wlr_surface *surface, struct wlr_box *_box, float rotation,
        void *_data) {
    bool *data = _data;
    bool whole = *data;

    struct wlr_box box = *_box;
    scale_box(&box, output->wlr_output->scale);

    int center_x = box.x + box.width/2;
    int center_y = box.y + box.height/2;

    if (pixman_region32_not_empty(&surface->buffer_damage)) {
        pixman_region32_t damage;
        pixman_region32_init(&damage);
        wlr_surface_get_effective_damage(surface, &damage);
        wlr_region_scale(&damage, &damage, output->wlr_output->scale);
        if (ceil(output->wlr_output->scale) > surface->current.scale) {
            // When scaling up a surface, it'll become blurry so we need to
            // expand the damage region
            wlr_region_expand(&damage, &damage,
                ceil(output->wlr_output->scale) - surface->current.scale);
        }
        pixman_region32_translate(&damage, box.x, box.y);
        wlr_region_rotated_bounds(&damage, &damage, rotation,
            center_x, center_y);
        wlr_output_damage_add(output->damage, &damage);
        pixman_region32_fini(&damage);
    }

    if (whole) {
        wlr_box_rotated_bounds(&box, &box, rotation);
        wlr_output_damage_add_box(output->damage, &box);
    }

    if (!wl_list_empty(&surface->current.frame_callback_list)) {
        wlr_output_schedule_frame(output->wlr_output);
    }
}

static void damage_child_views_iterator(struct sway_container *con,
        void *data) {
    if (!con->view || !view_is_visible(con->view)) {
        return;
    }
    struct sway_output *output = data;
    bool whole = true;
    output_view_for_each_surface(output, con->view, damage_surface_iterator,
            &whole);
}

void view_damage_from(struct sway_view *view) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        output_damage_from_view(output, view);
    }
}

void container_damage_whole(struct sway_container *container) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        output_damage_whole_container(output, container);
    }
}

void output_damage_box(struct sway_output *output, struct wlr_box *_box) {
    struct wlr_box box;
    memcpy(&box, _box, sizeof(struct wlr_box));
    box.x -= output->lx;
    box.y -= output->ly;
    scale_box(&box, output->wlr_output->scale);
    wlr_output_damage_add_box(output->damage, &box);
}

void output_damage_whole_container(struct sway_output *output,
        struct sway_container *con) {
    // Pad the box by 1px, because the width is a double and might be a fraction
    struct wlr_box box = {
        .x = con->current.x - output->lx - 1,
        .y = con->current.y - output->ly - 1,
        .width = con->current.width + 2,
        .height = con->current.height + 2,
    };
    scale_box(&box, output->wlr_output->scale);
    wlr_output_damage_add_box(output->damage, &box);
    // Damage subsurfaces as well, which may extend outside the box
    if (con->view) {
        damage_child_views_iterator(con, output);
    }
}

void output_damage_surface(struct sway_output *output, double ox, double oy,
        struct wlr_surface *surface, bool whole) {
    output_surface_for_each_surface(output, surface, ox, oy,
        damage_surface_iterator, &whole);
}

void output_damage_from_view(struct sway_output *output,
        struct sway_view *view) {
    if (!view_is_visible(view)) {
        return;
    }
    bool whole = false;
    output_view_for_each_surface(output, view, damage_surface_iterator, &whole);
}

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
