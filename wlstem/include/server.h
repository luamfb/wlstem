#ifndef WLSTEM_SERVER_H
#define WLSTEM_SERVER_H
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>

struct wls_server {
    struct wl_display *wl_display;
    struct wl_event_loop *wl_event_loop;
    struct wlr_backend *backend;
    bool running;
};

struct wls_server * wls_server_create(void);

void wls_server_run(struct wls_server *server, const char *socket);

void wls_server_terminate(struct wls_server *server);

void wls_server_destroy(struct wls_server *server);

#endif
