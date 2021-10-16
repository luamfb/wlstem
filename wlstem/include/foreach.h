#ifndef WLSTEM_FOREACH_H_
#define WLSTEM_FOREACH_H_

#include <wlr/types/wlr_surface.h>
#include "config.h"

// A header for each of the several FOO_for_each_BAR functions.

struct sway_container;
struct sway_output;
struct sway_view;

typedef void (*sway_surface_iterator_func_t)(struct sway_output *output, struct sway_view *view,
    struct wlr_surface *surface, struct wlr_box *box, float rotation,
    void *user_data);

// =============== OUTPUT_LAYOUT ===============
//
void wls_output_layout_for_each_output(void (*f)(struct sway_output *output, void *data),
        void *data);

void wls_output_layout_for_each_container(void (*f)(struct sway_container *con, void *data),
        void *data);

// =============== OUTPUTS ===============

void output_surface_for_each_surface(struct sway_output *output,
        struct wlr_surface *surface, double ox, double oy,
        sway_surface_iterator_func_t iterator, void *user_data);

void output_view_for_each_surface(struct sway_output *output,
    struct sway_view *view, sway_surface_iterator_func_t iterator,
    void *user_data);

void output_view_for_each_popup_surface(struct sway_output *output,
        struct sway_view *view, sway_surface_iterator_func_t iterator,
        void *user_data);

void output_layer_for_each_surface(struct sway_output *output,
    struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
    void *user_data);

void output_layer_for_each_toplevel_surface(struct sway_output *output,
    struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
    void *user_data);

void output_layer_for_each_popup_surface(struct sway_output *output,
    struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
    void *user_data);

#if HAVE_XWAYLAND
void output_unmanaged_for_each_surface(struct sway_output *output,
    struct wl_list *unmanaged, sway_surface_iterator_func_t iterator,
    void *user_data);
#endif

void output_drag_icons_for_each_surface(struct sway_output *output,
    struct wl_list *drag_icons, sway_surface_iterator_func_t iterator,
    void *user_data);

void output_for_each_container(struct sway_output *output,
        void (*f)(struct sway_container *con, void *data), void *data);

void output_for_each_surface(struct sway_output *output,
        sway_surface_iterator_func_t iterator, void *user_data);

// =============== VIEWS ===============

/**
 * Iterate all surfaces of a view (toplevels + popups).
 */
void view_for_each_surface(struct sway_view *view,
    wlr_surface_iterator_func_t iterator, void *user_data);

/**
 * Iterate all popup surfaces of a view.
 */
void view_for_each_popup_surface(struct sway_view *view,
    wlr_surface_iterator_func_t iterator, void *user_data);

#endif /* WLSTEM_FOREACH_H_ */
