#ifndef _SWAY_CONTAINER_H
#define _SWAY_CONTAINER_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include "list.h"
#include "sway/tree/node.h"

struct sway_view;
struct sway_seat;

struct sway_root;
struct sway_output;
struct sway_workspace;
struct sway_view;

enum wlr_direction;

struct sway_container_state {
    // Container properties
    double x, y;
    double width, height;

    struct sway_workspace *workspace;
    struct sway_container *parent;
    list_t *children;

    bool focused;

    int border_thickness;
    bool border_top;
    bool border_bottom;
    bool border_left;
    bool border_right;

    double content_x, content_y;
    double content_width, content_height;
};

struct sway_container {
    struct sway_node node;
    struct sway_view *view;

    // The pending state is the main container properties, and the current state is in the below struct.
    // This means most places of the code can refer to the main variables (pending state) and it'll just work.
    struct sway_container_state current;

    char *title;           // The view's title (unformatted)
    char *formatted_title; // The title displayed in the title bar

    // For C_ROOT, this has no meaning
    // For other types, this is the position in layout coordinates
    // Includes borders
    double x, y;
    double width, height;
    double saved_x, saved_y;
    double saved_width, saved_height;

    // The share of the space of parent container this container occupies
    double width_fraction;
    double height_fraction;

    // The share of space of the parent container that all children occupy
    // Used for doing the resize calculations
    double child_total_width;
    double child_total_height;

    // These are in layout coordinates.
    double content_x, content_y;
    int content_width, content_height;

    // In most cases this is the same as the content x and y, but if the view
    // refuses to resize to the content dimensions then it can be smaller.
    // These are in layout coordinates.
    double surface_x, surface_y;

    int border_thickness;
    bool border_top;
    bool border_bottom;
    bool border_left;
    bool border_right;

    struct sway_workspace *workspace;
    struct sway_container *parent;    // NULL if container in root of workspace
    list_t *children;                 // struct sway_container

    // Outputs currently being intersected
    list_t *outputs; // struct sway_output

    float alpha;

    struct wlr_texture *title_focused;
    struct wlr_texture *title_unfocused;
    struct wlr_texture *title_urgent;
    size_t title_height;
    size_t title_baseline;

    struct {
        struct wl_signal destroy;
    } events;
};

struct sway_container *container_create(struct sway_view *view);

void container_destroy(struct sway_container *con);

void container_begin_destroy(struct sway_container *con);

/**
 * Find a container at the given coordinates. Returns the surface and
 * surface-local coordinates of the given layout coordinates if the container
 * is a view and the view contains a surface at those coordinates.
 */
struct sway_container *container_at(struct sway_workspace *workspace,
        double lx, double ly, struct wlr_surface **surface,
        double *sx, double *sy);

struct sway_container *tiling_container_at(
        struct sway_node *parent, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy);

void container_for_each_child(struct sway_container *container,
        void (*f)(struct sway_container *container, void *data), void *data);

/**
 * Returns true if the given container is an ancestor of this container.
 */
bool container_has_ancestor(struct sway_container *container,
        struct sway_container *ancestor);

void container_update_textures_recursive(struct sway_container *con);

void container_damage_whole(struct sway_container *container);

void container_reap_empty(struct sway_container *con);

void container_update_title_textures(struct sway_container *container);

/**
 * Calculate the container's title_height property.
 */
void container_calculate_title_height(struct sway_container *container);

/**
 * Return the height of a regular title bar.
 */
size_t container_titlebar_height(void);

void container_set_resizing(struct sway_container *con, bool resizing);

/**
 * Get a container's box in layout coordinates.
 */
void container_get_box(struct sway_container *container, struct wlr_box *box);

/**
 * If the container is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void container_end_mouse_operation(struct sway_container *container);

/**
 * Walk up the container tree branch starting at the given container, and return
 * its earliest ancestor.
 */
struct sway_container *container_toplevel_ancestor(
        struct sway_container *container);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct sway_output *container_get_effective_output(struct sway_container *con);

void container_discover_outputs(struct sway_container *con);

list_t *container_get_siblings(struct sway_container *container);

int container_sibling_index(struct sway_container *child);

list_t *container_get_current_siblings(struct sway_container *container);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void container_add_sibling(struct sway_container *parent,
        struct sway_container *child, bool after);

void container_detach(struct sway_container *child);

void container_replace(struct sway_container *container,
        struct sway_container *replacement);

#endif
