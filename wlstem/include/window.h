#ifndef _SWAY_WINDOW_H
#define _SWAY_WINDOW_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include "list.h"
#include "node.h"

struct sway_view;
struct sway_seat;

struct sway_output;
struct sway_view;

enum wlr_direction;

struct wls_window_state {
    // Window properties
    double x, y;
    double width, height;

    struct sway_output *output;

    bool focused;

    double content_x, content_y;
    double content_width, content_height;
};

struct wls_window {
    struct wls_transaction_node node;
    struct sway_view *view;

    // The pending state is the main window properties, and the current state is in the below struct.
    // This means most places of the code can refer to the main variables (pending state) and it'll just work.
    struct wls_window_state current;

    char *title;           // The view's title

    // For C_ROOT, this has no meaning
    // For other types, this is the position in layout coordinates
    // Includes borders
    double x, y;
    double width, height;
    double saved_x, saved_y;
    double saved_width, saved_height;

    // These are in layout coordinates.
    double content_x, content_y;
    int content_width, content_height;

    // In most cases this is the same as the content x and y, but if the view
    // refuses to resize to the content dimensions then it can be smaller.
    // These are in layout coordinates.
    double surface_x, surface_y;

    struct sway_output *output;

    // Outputs currently being intersected
    list_t *outputs; // struct sway_output

    float alpha;

    size_t title_height;
    size_t title_baseline;

    struct {
        struct wl_signal destroy;
        struct wl_signal scale_change;
    } events;

    void *data; // custom user data
};

struct wls_window *window_create(struct sway_view *view);

void window_destroy(struct wls_window *win);

void window_begin_destroy(struct wls_window *win);

/**
 * Find a window at the given coordinates. Returns the surface and
 * surface-local coordinates of the given layout coordinates if the window
 * is a view and the view contains a surface at those coordinates.
 */
struct wls_window *window_at(struct sway_output *output,
        double lx, double ly, struct wlr_surface **surface,
        double *sx, double *sy);

struct wls_window *toplevel_window_at(
        struct wls_transaction_node *parent, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy);

void window_set_resizing(struct wls_window *win, bool resizing);

/**
 * Get a window's box in layout coordinates.
 */
void window_get_box(struct wls_window *window, struct wlr_box *box);

/**
 * If the window is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void window_end_mouse_operation(struct wls_window *window);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct sway_output *window_get_effective_output(struct wls_window *win);

void window_discover_outputs(struct wls_window *win);

list_t *window_get_siblings(struct wls_window *window);

int window_sibling_index(struct wls_window *child);

list_t *window_get_current_siblings(struct wls_window *window);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void window_add_sibling(struct wls_window *parent,
        struct wls_window *child, bool after);

void window_detach(struct wls_window *child);

void window_replace(struct wls_window *window,
        struct wls_window *replacement);

bool surface_is_popup(struct wlr_surface *surface);

struct wls_window *surface_at_view(struct wls_window *win, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy);

#endif
