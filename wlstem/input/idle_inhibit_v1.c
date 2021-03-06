#include <stdlib.h>
#include <wlr/types/wlr_idle.h>
#include "log.h"
#include "idle_inhibit_v1.h"
#include "seat.h"
#include "view.h"
#include "window.h"
#include "wlstem.h"

static void destroy_inhibitor(struct sway_idle_inhibitor_v1 *inhibitor) {
    wl_list_remove(&inhibitor->link);
    wl_list_remove(&inhibitor->destroy.link);
    sway_idle_inhibit_v1_check_active(inhibitor->manager);
    free(inhibitor);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
    struct sway_idle_inhibitor_v1 *inhibitor =
        wl_container_of(listener, inhibitor, destroy);
    sway_log(SWAY_DEBUG, "Sway idle inhibitor destroyed");
    destroy_inhibitor(inhibitor);
}

void handle_idle_inhibitor_v1(struct wl_listener *listener, void *data) {
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
    struct sway_idle_inhibit_manager_v1 *manager =
        wl_container_of(listener, manager, new_idle_inhibitor_v1);
    sway_log(SWAY_DEBUG, "New sway idle inhibitor");

    struct sway_idle_inhibitor_v1 *inhibitor =
        calloc(1, sizeof(struct sway_idle_inhibitor_v1));
    if (!inhibitor) {
        return;
    }

    inhibitor->manager = manager;
    inhibitor->mode = INHIBIT_IDLE_APPLICATION;
    inhibitor->view = view_from_wlr_surface(wlr_inhibitor->surface);
    wl_list_insert(&manager->inhibitors, &inhibitor->link);

    inhibitor->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

    sway_idle_inhibit_v1_check_active(manager);
}

void sway_idle_inhibit_v1_user_inhibitor_register(struct sway_view *view,
        enum sway_idle_inhibit_mode mode) {
    struct sway_idle_inhibitor_v1 *inhibitor =
        calloc(1, sizeof(struct sway_idle_inhibitor_v1));
    if (!inhibitor) {
        return;
    }

    inhibitor->manager = wls->misc_protocols->idle_inhibit_manager_v1;
    inhibitor->mode = mode;
    inhibitor->view = view;
    wl_list_insert(&inhibitor->manager->inhibitors, &inhibitor->link);

    inhibitor->destroy.notify = handle_destroy;
    wl_signal_add(&view->events.unmap, &inhibitor->destroy);

    sway_idle_inhibit_v1_check_active(inhibitor->manager);
}

struct sway_idle_inhibitor_v1 *sway_idle_inhibit_v1_user_inhibitor_for_view(
        struct sway_view *view) {
    struct sway_idle_inhibitor_v1 *inhibitor;
    wl_list_for_each(inhibitor, &wls->misc_protocols->idle_inhibit_manager_v1->inhibitors,
            link) {
        if (inhibitor->view == view &&
                inhibitor->mode != INHIBIT_IDLE_APPLICATION) {
            return inhibitor;
        }
    }
    return NULL;
}

struct sway_idle_inhibitor_v1 *sway_idle_inhibit_v1_application_inhibitor_for_view(
        struct sway_view *view) {
    struct sway_idle_inhibitor_v1 *inhibitor;
    wl_list_for_each(inhibitor, &wls->misc_protocols->idle_inhibit_manager_v1->inhibitors,
            link) {
        if (inhibitor->view == view &&
                inhibitor->mode == INHIBIT_IDLE_APPLICATION) {
            return inhibitor;
        }
    }
    return NULL;
}

void sway_idle_inhibit_v1_user_inhibitor_destroy(
        struct sway_idle_inhibitor_v1 *inhibitor) {
    if (!inhibitor) {
        return;
    }
    if (!sway_assert(inhibitor->mode != INHIBIT_IDLE_APPLICATION,
                "User should not be able to destroy application inhibitor")) {
        return;
    }
    destroy_inhibitor(inhibitor);
}

bool sway_idle_inhibit_v1_is_active(struct sway_idle_inhibitor_v1 *inhibitor) {
    switch (inhibitor->mode) {
    case INHIBIT_IDLE_APPLICATION:
        // If there is no view associated with the inhibitor, assume visible
        return !inhibitor->view || !inhibitor->view->window ||
            view_is_visible(inhibitor->view);
    case INHIBIT_IDLE_FOCUS:;
        struct sway_seat *seat = NULL;
        wl_list_for_each(seat, &wls->seats, link) {
            struct wls_window *win = seat_get_focused_window(seat);
            if (win && win->view && win->view == inhibitor->view) {
                return true;
            }
        }
        return false;
    case INHIBIT_IDLE_FULLSCREEN:
        return false;
    case INHIBIT_IDLE_OPEN:
        // Inhibitor is destroyed on unmap so it must be open/mapped
        return true;
    case INHIBIT_IDLE_VISIBLE:
        return view_is_visible(inhibitor->view);
    }
    return false;
}

void sway_idle_inhibit_v1_check_active(
        struct sway_idle_inhibit_manager_v1 *manager) {
    struct sway_idle_inhibitor_v1 *inhibitor;
    bool inhibited = false;
    wl_list_for_each(inhibitor, &manager->inhibitors, link) {
        if ((inhibited = sway_idle_inhibit_v1_is_active(inhibitor))) {
            break;
        }
    }
    wlr_idle_set_enabled(manager->idle, NULL, !inhibited);
}

struct sway_idle_inhibit_manager_v1 *sway_idle_inhibit_manager_v1_create(
        struct wl_display *wl_display, struct wlr_idle *idle) {
    struct sway_idle_inhibit_manager_v1 *manager =
        calloc(1, sizeof(struct sway_idle_inhibit_manager_v1));
    if (!manager) {
        return NULL;
    }

    manager->wlr_manager = wlr_idle_inhibit_v1_create(wl_display);
    if (!manager->wlr_manager) {
        free(manager);
        return NULL;
    }
    manager->idle = idle;
    wl_signal_add(&manager->wlr_manager->events.new_inhibitor,
        &manager->new_idle_inhibitor_v1);
    manager->new_idle_inhibitor_v1.notify = handle_idle_inhibitor_v1;
    wl_list_init(&manager->inhibitors);

    return manager;
}
