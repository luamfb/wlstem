#ifndef _SWAY_ARRANGE_H
#define _SWAY_ARRANGE_H

#include <wayland-server-core.h>

struct sway_output;
struct sway_container;

struct server_wm {
    struct wl_listener output_layout_change;
    struct wl_listener output_connected;
    struct wl_listener output_disconnected;

    struct wl_listener new_window;
};

struct server_wm * server_wm_create(void);
void server_wm_destroy(struct server_wm *wm);

void arrange_container(struct sway_container *container);

void arrange_output(struct sway_output *output);

void arrange_root(void);

void arrange_output_layout(void);

#endif
