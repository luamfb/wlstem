#ifndef SERVER_WINDOW_TITLE_H_
#define SERVER_WINDOW_TITLE_H_

#include <wayland-server-core.h>
#include <wlr/render/wlr_texture.h>

struct window_title {
    char *formatted_title; // Formatted title displayed in the title bar
    struct wlr_texture *title_focused;
    struct wlr_texture *title_unfocused;
    struct wlr_texture *title_urgent;

    struct wl_listener container_destroyed;
    struct wl_listener scale_changed;
};

#endif /* SERVER_WINDOW_TITLE_H_ */
