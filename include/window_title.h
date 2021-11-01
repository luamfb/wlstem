#ifndef SERVER_WINDOW_TITLE_H_
#define SERVER_WINDOW_TITLE_H_

#include <wayland-server-core.h>
#include <wlr/render/wlr_texture.h>

struct sway_container;

struct window_title {
    char *formatted_title; // Formatted title displayed in the title bar
    struct wlr_texture *title_focused;
    struct wlr_texture *title_unfocused;
    struct wlr_texture *title_urgent;

    struct wl_listener container_destroyed;
    struct wl_listener scale_changed;
};

void container_update_title_textures(struct sway_container *container);

/**
 * Calculate the container's title_height property.
 */
void container_calculate_title_height(struct sway_container *container);

/**
 * Return the height of a regular title bar.
 */
size_t container_titlebar_height(void);


#endif /* SERVER_WINDOW_TITLE_H_ */
