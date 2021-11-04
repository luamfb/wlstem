#ifndef SERVER_WINDOW_TITLE_H_
#define SERVER_WINDOW_TITLE_H_

#include <wayland-server-core.h>
#include <wlr/render/wlr_texture.h>

struct wls_window;

struct window_title {
    char *formatted_title; // Formatted title displayed in the title bar
    struct wlr_texture *title_focused;
    struct wlr_texture *title_unfocused;
    struct wlr_texture *title_urgent;

    struct wl_listener window_destroyed;
    struct wl_listener scale_changed;
};

void window_update_title_textures(struct wls_window *window);

/**
 * Calculate the window's title_height property.
 */
void window_calculate_title_height(struct wls_window *window);

/**
 * Return the height of a regular title bar.
 */
size_t window_titlebar_height(void);


#endif /* SERVER_WINDOW_TITLE_H_ */
