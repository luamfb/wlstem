#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include "log.h"
#include "server.h"

static bool server_privileged_prepare(struct wls_server *server) {
    sway_log(SWAY_DEBUG, "Preparing Wayland server initialization");
    server->wl_display = wl_display_create();
    server->wl_event_loop = wl_display_get_event_loop(server->wl_display);
    server->backend = wlr_backend_autocreate(server->wl_display);

    if (!server->backend) {
        sway_log(SWAY_ERROR,
            "Unable to create backend.\n"
            "\tIf you don't have logind, you may need a setuid binary for\n"
            "\tproper initialization (privileges will be dropped on startup).\n"
            "\tHowever, do NOT run the Wayland server as root.");
        return false;
    }
    return true;
}

static bool drop_permissions(void) {
    if (getuid() != geteuid() || getgid() != getegid()) {
        // Set the gid and uid in the correct order.
        if (setgid(getgid()) != 0) {
            sway_log(SWAY_ERROR, "Unable to drop root group, refusing to start");
            return false;
        }
        if (setuid(getuid()) != 0) {
            sway_log(SWAY_ERROR, "Unable to drop root user, refusing to start");
            return false;
        }
    }
    if (setgid(0) != -1 || setuid(0) != -1) {
        sway_log(SWAY_ERROR,
            "Unable to drop root privileges, refusing to start.\n"
            "\tMake sure you are NOT running the Wayland server as root.\n"
            "\tIf needed, make the binary setuid so privileges can be dropped.\n");
        return false;
    }
    return true;
}

struct wls_server * wls_server_create(void) {
    struct wls_server *server = calloc(1, sizeof(struct wls_server));
    if (!server_privileged_prepare(server)) {
        return NULL;
    }
    if (!drop_permissions()) {
        wl_display_destroy_clients(server->wl_display);
        wl_display_destroy(server->wl_display);
        return NULL;
    }
    server->running = false;
    return server;
}

void wls_server_terminate(struct wls_server *server) {
    if (!server || !server->running) {
        return;
    }
    wl_display_terminate(server->wl_display);
    server->running = false;
}

void wls_server_run(struct wls_server *server, const char *socket) {
    sway_log(SWAY_INFO, "Running compositor on wayland display '%s'",
            socket);
    server->running = true;
    wl_display_run(server->wl_display);
}

void wls_server_destroy(struct wls_server *server) {
    if (!server) {
        return;
    }
    if (server->running) {
        wls_server_terminate(server);
    }
    wl_display_destroy_clients(server->wl_display);
    wl_display_destroy(server->wl_display);
}
