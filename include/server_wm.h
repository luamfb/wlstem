#ifndef SERVER_WM_H_
#define SERVER_WM_H_

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>

struct sway_output;
struct sway_container;

struct server_wm {
    struct wl_listener output_layout_change;
    struct wl_listener output_connected;
    struct wl_listener output_disconnected;
    struct wl_listener output_mode_changed;

    struct wl_listener new_window;
};

void handle_output_commit(struct sway_output *output,
    struct wlr_output_event_commit *event);


struct server_wm * server_wm_create(void);
void server_wm_destroy(struct server_wm *wm);

void arrange_container(struct sway_container *container);

void arrange_output(struct sway_output *output);

void arrange_root(void);

void arrange_output_layout(void);

#endif
