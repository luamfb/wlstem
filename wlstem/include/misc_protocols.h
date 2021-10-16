#ifndef WLSTEM_MISC_PROTOCOLS
#define WLSTEM_MISC_PROTOCOLS

#include <wlr/types/wlr_idle.h>
#include "idle_inhibit_v1.h"

struct wls_server;

struct wls_misc_protocols {
    struct wlr_idle *idle;
    struct sway_idle_inhibit_manager_v1 *idle_inhibit_manager_v1;
};

struct wls_misc_protocols * wls_create_misc_protocols(struct wl_display *display);

#endif /* WLSTEM_MISC_PROTOCOLS */
