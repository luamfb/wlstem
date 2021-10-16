#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "misc_protocols.h"
#include "log.h"
#include "server.h"

struct wls_misc_protocols * wls_create_misc_protocols(struct wl_display *display) {
    struct wls_misc_protocols *misc_proto =
        calloc(1, sizeof(struct wls_misc_protocols));

    if (!misc_proto) {
        sway_log(SWAY_ERROR, "misc protocols allocation failed");
        return NULL;
    }

    struct wlr_idle *_idle =  wlr_idle_create(display);
    if (!_idle) {
        sway_log(SWAY_ERROR, "wlr_idle creation failed");
        return NULL;
    }

    struct sway_idle_inhibit_manager_v1 *_idle_inhibit_v1 = 
        sway_idle_inhibit_manager_v1_create(display, _idle);
    if (!_idle_inhibit_v1) {
        sway_log(SWAY_ERROR, "sway_idle_inhibit_v1 creation failed");
        return NULL;
    }

    misc_proto->idle = _idle;
    misc_proto->idle_inhibit_manager_v1 = _idle_inhibit_v1;
    
    return misc_proto;
}
