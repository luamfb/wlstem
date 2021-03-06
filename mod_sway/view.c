#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include "config.h"
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "damage.h"
#include "idle_inhibit_v1.h"
#include "foreach.h"
#include "list.h"
#include "log.h"
#include "sway_commands.h"
#include "transaction.h"
#include "window_title.h"
#include "cursor.h"
#include "output.h"
#include "seat.h"
#include "sway_server.h"
#include "server_arrange.h"
#include "server_wm.h"
#include "window.h"
#include "view.h"
#include "sway_config.h"
#include "sway_xdg_decoration.h"
#include "pango.h"
#include "stringop.h"
#include "wlstem.h"

void view_init(struct sway_view *view, enum sway_view_type type,
        const struct sway_view_impl *impl) {
    view->type = type;
    view->impl = impl;
    wl_list_init(&view->saved_buffers);
    view->allow_request_urgent = true;
    view->shortcuts_inhibit = OPT_UNSET;
    wl_signal_init(&view->events.unmap);
}

void view_begin_destroy(struct sway_view *view) {
    if (!sway_assert(view->surface == NULL, "Tried to destroy a mapped view")) {
        return;
    }
    view->destroying = true;

    if (!view->window) {
        view_destroy(view);
    }
}

const char *view_get_title(struct sway_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_TITLE);
    }
    return NULL;
}

const char *view_get_app_id(struct sway_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_APP_ID);
    }
    return NULL;
}

const char *view_get_class(struct sway_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_CLASS);
    }
    return NULL;
}

const char *view_get_instance(struct sway_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_INSTANCE);
    }
    return NULL;
}
#if HAVE_XWAYLAND
uint32_t view_get_x11_window_id(struct sway_view *view) {
    if (view->impl->get_int_prop) {
        return view->impl->get_int_prop(view, VIEW_PROP_X11_WINDOW_ID);
    }
    return 0;
}

uint32_t view_get_x11_parent_id(struct sway_view *view) {
    if (view->impl->get_int_prop) {
        return view->impl->get_int_prop(view, VIEW_PROP_X11_PARENT_ID);
    }
    return 0;
}
#endif
const char *view_get_window_role(struct sway_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_WINDOW_ROLE);
    }
    return NULL;
}

uint32_t view_get_window_type(struct sway_view *view) {
    if (view->impl->get_int_prop) {
        return view->impl->get_int_prop(view, VIEW_PROP_WINDOW_TYPE);
    }
    return 0;
}

const char *view_get_shell(struct sway_view *view) {
    switch(view->type) {
    case SWAY_VIEW_XDG_SHELL:
        return "xdg_shell";
#if HAVE_XWAYLAND
    case SWAY_VIEW_XWAYLAND:
        return "xwayland";
#endif
    }
    return "unknown";
}

void view_get_constraints(struct sway_view *view, double *min_width,
        double *max_width, double *min_height, double *max_height) {
    if (view->impl->get_constraints) {
        view->impl->get_constraints(view,
                min_width, max_width, min_height, max_height);
    } else {
        *min_width = DBL_MIN;
        *max_width = DBL_MAX;
        *min_height = DBL_MIN;
        *max_height = DBL_MAX;
    }
}

bool view_inhibit_idle(struct sway_view *view) {
    struct sway_idle_inhibitor_v1 *user_inhibitor =
        sway_idle_inhibit_v1_user_inhibitor_for_view(view);

    struct sway_idle_inhibitor_v1 *application_inhibitor =
        sway_idle_inhibit_v1_application_inhibitor_for_view(view);

    if (!user_inhibitor && !application_inhibitor) {
        return false;
    }

    if (!user_inhibitor) {
        return sway_idle_inhibit_v1_is_active(application_inhibitor);
    }

    if (!application_inhibitor) {
        return sway_idle_inhibit_v1_is_active(user_inhibitor);
    }

    return sway_idle_inhibit_v1_is_active(user_inhibitor)
        || sway_idle_inhibit_v1_is_active(application_inhibitor);
}

bool view_ancestor_is_only_visible(struct sway_view *view) {
    return true;
}

void view_autoconfigure(struct sway_view *view) {
    struct wls_window *win = view->window;

    double x, y, width, height;
    // Height is: 1px border + 3px pad + title height + 3px pad + 1px border
    x = win->x + config->border_thickness;
    width = win->width - config->border_thickness - config->border_thickness;
    y = win->y + window_titlebar_height();
    height = win->height - window_titlebar_height()
        - config->border_thickness;

    win->content_x = x;
    win->content_y = y;
    win->content_width = width;
    win->content_height = height;
}

void view_set_activated(struct sway_view *view, bool activated) {
    if (view->impl->set_activated) {
        view->impl->set_activated(view, activated);
    }
    if (view->foreign_toplevel) {
        wlr_foreign_toplevel_handle_v1_set_activated(
                view->foreign_toplevel, activated);
    }
}

void view_request_activate(struct sway_view *view) {
    struct sway_output *output = view->window->output;
    if (!output) {
        return;
    }
    if (!output->active) {
        return;
    }
    view_set_urgent(view, true);
}

void view_set_csd_from_server(struct sway_view *view, bool enabled) {
    sway_log(SWAY_DEBUG, "Telling view %p to set CSD to %i", view, enabled);
    if (view->xdg_decoration) {
        uint32_t mode = enabled ?
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE :
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        wlr_xdg_toplevel_decoration_v1_set_mode(
                view->xdg_decoration->wlr_xdg_decoration, mode);
    }
    view->using_csd = enabled;
}

void view_update_csd_from_client(struct sway_view *view, bool enabled) {
    sway_log(SWAY_DEBUG, "View %p updated CSD to %i", view, enabled);
    view->using_csd = enabled;
}

void view_set_tiled(struct sway_view *view, bool tiled) {
    if (view->impl->set_tiled) {
        view->impl->set_tiled(view, tiled);
    }
}

void view_close(struct sway_view *view) {
    if (view->impl->close) {
        view->impl->close(view);
    }
}

void view_close_popups(struct sway_view *view) {
    if (view->impl->close_popups) {
        view->impl->close_popups(view);
    }
}

static void view_subsurface_create(struct sway_view *view,
    struct wlr_subsurface *subsurface);

static void view_init_subsurfaces(struct sway_view *view,
    struct wlr_surface *surface);

static void view_handle_surface_new_subsurface(struct wl_listener *listener,
        void *data) {
    struct sway_view *view =
        wl_container_of(listener, view, surface_new_subsurface);
    struct wlr_subsurface *subsurface = data;
    view_subsurface_create(view, subsurface);
}

static void view_populate_pid(struct sway_view *view) {
    pid_t pid;
    switch (view->type) {
#if HAVE_XWAYLAND
    case SWAY_VIEW_XWAYLAND:;
        struct wlr_xwayland_surface *surf =
            wlr_xwayland_surface_from_wlr_surface(view->surface);
        pid = surf->pid;
        break;
#endif
    case SWAY_VIEW_XDG_SHELL:;
        struct wl_client *client =
            wl_resource_get_client(view->surface->resource);
        wl_client_get_credentials(client, &pid, NULL, NULL);
        break;
    }
    view->pid = pid;
}

static struct sway_output *select_output(struct sway_view *view) {
    struct sway_seat *seat = input_manager_current_seat();

    // Use the focused workspace
    struct wls_transaction_node *node = seat_get_next_in_focus_stack(seat);
    if (node && node->type == N_OUTPUT) {
        struct sway_output *output = node->sway_output;
        if (output->active) {
            return output;
        }
    } else if (node && node->type == N_WINDOW) {
        struct sway_output *output = node->wls_window->output;
        if (!sway_assert(output, "window has no output")) {
            abort();
        }
        if (output->active) {
            return node->wls_window->output;
        }
    } else if (node) {
        sway_log(SWAY_ERROR,
            "%s: unknown node type %d (%x) from node ID %lu",
            __func__,
            node->type,
            node->type,
            node->id);
        abort();
    }
    sway_log(SWAY_DEBUG, "no active outputs found");
    return NULL;
}

static bool should_focus(struct sway_view *view) {
    struct sway_seat *seat = input_manager_current_seat();
    struct wls_window *prev_con = seat_get_focused_window(seat);
    struct sway_output *prev_output = seat_get_focused_output(seat);
    struct sway_output *map_output = view->window->output;

    // Views can only take focus if they are mapped into the active output
    if (map_output && prev_output != map_output) {
        return false;
    }

    // If the view is the only one in the focused workspace, it'll get focus
    if (!prev_con) {
        struct sway_output *output = view->window->output;
        if (!output) {
            sway_log(SWAY_DEBUG, "workspace has no output!");
            return false;
        }
        size_t num_children = output->windows->length;
        if (num_children == 1) {
            return true;
        }
    }

    return true;
}

static void handle_foreign_activate_request(
        struct wl_listener *listener, void *data) {
    struct sway_view *view = wl_container_of(
            listener, view, foreign_activate_request);
    struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
    struct sway_seat *seat;
    wl_list_for_each(seat, &wls->seats, link) {
        if (seat->wlr_seat == event->seat) {
            seat_set_focus_window(seat, view->window);
            seat_consider_warp_to_focus(seat);
            break;
        }
    }
}

static void handle_foreign_fullscreen_request(struct wl_listener *listener, void *data) {
}

static void handle_foreign_close_request(
        struct wl_listener *listener, void *data) {
    struct sway_view *view = wl_container_of(
            listener, view, foreign_close_request);
    view_close(view);
}

static void handle_foreign_destroy(
        struct wl_listener *listener, void *data) {
    struct sway_view *view = wl_container_of(
            listener, view, foreign_destroy);

    wl_list_remove(&view->foreign_activate_request.link);
    wl_list_remove(&view->foreign_fullscreen_request.link);
    wl_list_remove(&view->foreign_close_request.link);
    wl_list_remove(&view->foreign_destroy.link);
}

void view_map(struct sway_view *view, struct wlr_surface *wlr_surface,
              bool fullscreen, struct wlr_output *fullscreen_output,
              bool decoration) {
    if (!sway_assert(view->surface == NULL, "cannot map mapped view")) {
        return;
    }
    view->surface = wlr_surface;
    view_populate_pid(view);
    view->window = window_create(view);

    struct sway_output *output = select_output(view);

    struct sway_seat *seat = input_manager_current_seat();
    struct wls_transaction_node *node = output ? seat_get_focus_inactive(seat, &output->node)
        : seat_get_next_in_focus_stack(seat);
    struct wls_window *target_sibling = node->type == N_WINDOW ?
        node->wls_window : NULL;

    view->foreign_toplevel =
        wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel_manager);
    view->foreign_activate_request.notify = handle_foreign_activate_request;
    wl_signal_add(&view->foreign_toplevel->events.request_activate,
            &view->foreign_activate_request);
    view->foreign_fullscreen_request.notify = handle_foreign_fullscreen_request;
    wl_signal_add(&view->foreign_toplevel->events.request_fullscreen,
            &view->foreign_fullscreen_request);
    view->foreign_close_request.notify = handle_foreign_close_request;
    wl_signal_add(&view->foreign_toplevel->events.request_close,
            &view->foreign_close_request);
    view->foreign_destroy.notify = handle_foreign_destroy;
    wl_signal_add(&view->foreign_toplevel->events.destroy,
            &view->foreign_destroy);

    struct wls_window *window = view->window;
    if (target_sibling) {
        window_add_sibling(target_sibling, window, 1);
    } else if (output) {
        if (!output->active) {
            sway_log(SWAY_DEBUG, "output is not active...");
            return;
        }
        window = output_add_window(output, window);
    }

    view_init_subsurfaces(view, wlr_surface);
    wl_signal_add(&wlr_surface->events.new_subsurface,
        &view->surface_new_subsurface);
    view->surface_new_subsurface.notify = view_handle_surface_new_subsurface;

    if (decoration) {
        view_update_csd_from_client(view, decoration);
    }

    view_set_tiled(view, true);

    view_update_title(view, false);

    if (window->output) {
        arrange_output(window->output);
    }

    if (should_focus(view)) {
        input_manager_set_focus(&view->window->node);
    }

    const char *app_id;
    const char *class;
    if ((app_id = view_get_app_id(view)) != NULL) {
        wlr_foreign_toplevel_handle_v1_set_app_id(
                view->foreign_toplevel, app_id);
    } else if ((class = view_get_class(view)) != NULL) {
        wlr_foreign_toplevel_handle_v1_set_app_id(
                view->foreign_toplevel, class);
    }
}

void view_unmap(struct sway_view *view) {
    wl_signal_emit(&view->events.unmap, view);

    wl_list_remove(&view->surface_new_subsurface.link);

    if (view->urgent_timer) {
        wl_event_source_remove(view->urgent_timer);
        view->urgent_timer = NULL;
    }

    if (view->foreign_toplevel) {
        wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
        view->foreign_toplevel = NULL;
    }

    struct sway_output *output = view->window->output;
    window_begin_destroy(view->window);

    if (output && output->active && !output->node.destroying) {
        arrange_output(output);
    }

    struct sway_seat *seat;
    wl_list_for_each(seat, &wls->seats, link) {
        seat->cursor->image_surface = NULL;
        if (seat->cursor->active_constraint) {
            struct wlr_surface *constrain_surface =
                seat->cursor->active_constraint->surface;
            if (view_from_wlr_surface(constrain_surface) == view) {
                sway_cursor_constrain(seat->cursor, NULL);
            }
        }
        seat_consider_warp_to_focus(seat);
    }

    transaction_commit_dirty();
    view->surface = NULL;
}

void view_update_size(struct sway_view *view, int width, int height) {
    struct wls_window *win = view->window;

    win->surface_x = win->content_x + (win->content_width - width) / 2;
    win->surface_y = win->content_y + (win->content_height - height) / 2;
    win->surface_x = fmax(win->surface_x, win->content_x);
    win->surface_y = fmax(win->surface_y, win->content_y);
}

static const struct sway_view_child_impl subsurface_impl;

static void subsurface_get_root_coords(struct sway_view_child *child,
        int *root_sx, int *root_sy) {
    struct wlr_surface *surface = child->surface;
    *root_sx = -child->view->geometry.x;
    *root_sy = -child->view->geometry.y;

    if (child->parent && child->parent->impl &&
            child->parent->impl->get_root_coords) {
        int sx, sy;
        child->parent->impl->get_root_coords(child->parent, &sx, &sy);
        *root_sx += sx;
        *root_sy += sy;
    } else {
        while (surface && wlr_surface_is_subsurface(surface)) {
            struct wlr_subsurface *subsurface =
                wlr_subsurface_from_wlr_surface(surface);
            if (subsurface == NULL) {
                break;
            }
            *root_sx += subsurface->current.x;
            *root_sy += subsurface->current.y;
            surface = subsurface->parent;
        }
    }
}

static void subsurface_destroy(struct sway_view_child *child) {
    if (!sway_assert(child->impl == &subsurface_impl,
            "Expected a subsurface")) {
        return;
    }
    struct sway_subsurface *subsurface = (struct sway_subsurface *)child;
    wl_list_remove(&subsurface->destroy.link);
    free(subsurface);
}

static const struct sway_view_child_impl subsurface_impl = {
    .get_root_coords = subsurface_get_root_coords,
    .destroy = subsurface_destroy,
};

static void subsurface_handle_destroy(struct wl_listener *listener,
        void *data) {
    struct sway_subsurface *subsurface =
        wl_container_of(listener, subsurface, destroy);
    struct sway_view_child *child = &subsurface->child;
    view_child_destroy(child);
}

static void view_child_damage(struct sway_view_child *child, bool whole);

static void view_subsurface_create(struct sway_view *view,
        struct wlr_subsurface *wlr_subsurface) {
    struct sway_subsurface *subsurface =
        calloc(1, sizeof(struct sway_subsurface));
    if (subsurface == NULL) {
        sway_log(SWAY_ERROR, "Allocation failed");
        return;
    }
    view_child_init(&subsurface->child, &subsurface_impl, view,
        wlr_subsurface->surface);

    wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
    subsurface->destroy.notify = subsurface_handle_destroy;

    subsurface->child.mapped = true;

    view_child_damage(&subsurface->child, true);
}

static void view_child_subsurface_create(struct sway_view_child *child,
        struct wlr_subsurface *wlr_subsurface) {
    struct sway_subsurface *subsurface =
        calloc(1, sizeof(struct sway_subsurface));
    if (subsurface == NULL) {
        sway_log(SWAY_ERROR, "Allocation failed");
        return;
    }
    subsurface->child.parent = child;
    wl_list_insert(&child->children, &subsurface->child.link);
    view_child_init(&subsurface->child, &subsurface_impl, child->view,
        wlr_subsurface->surface);

    wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
    subsurface->destroy.notify = subsurface_handle_destroy;

    subsurface->child.mapped = true;

    view_child_damage(&subsurface->child, true);
}

static void view_child_damage(struct sway_view_child *child, bool whole) {
    if (!child || !child->mapped || !child->view || !child->view->window) {
        return;
    }
    int sx, sy;
    child->impl->get_root_coords(child, &sx, &sy);
    desktop_damage_surface(child->surface,
            child->view->window->content_x + sx,
            child->view->window->content_y + sy, whole);
}

static void view_child_handle_surface_commit(struct wl_listener *listener,
        void *data) {
    struct sway_view_child *child =
        wl_container_of(listener, child, surface_commit);
    view_child_damage(child, false);
}

static void view_child_handle_surface_new_subsurface(
        struct wl_listener *listener, void *data) {
    struct sway_view_child *child =
        wl_container_of(listener, child, surface_new_subsurface);
    struct wlr_subsurface *subsurface = data;
    view_child_subsurface_create(child, subsurface);
}

static void view_child_handle_surface_destroy(struct wl_listener *listener,
        void *data) {
    struct sway_view_child *child =
        wl_container_of(listener, child, surface_destroy);
    view_child_destroy(child);
}

static void view_init_subsurfaces(struct sway_view *view,
        struct wlr_surface *surface) {
    struct wlr_subsurface *subsurface;
    wl_list_for_each(subsurface, &surface->subsurfaces, parent_link) {
        view_subsurface_create(view, subsurface);
    }
}

static void view_child_handle_surface_map(struct wl_listener *listener,
        void *data) {
    struct sway_view_child *child =
        wl_container_of(listener, child, surface_map);
    child->mapped = true;
    view_child_damage(child, true);
}

static void view_child_handle_surface_unmap(struct wl_listener *listener,
        void *data) {
    struct sway_view_child *child =
        wl_container_of(listener, child, surface_unmap);
    view_child_damage(child, true);
    child->mapped = false;
}

static void view_child_handle_view_unmap(struct wl_listener *listener,
        void *data) {
    struct sway_view_child *child =
        wl_container_of(listener, child, view_unmap);
    view_child_damage(child, true);
    child->mapped = false;
}

void view_child_init(struct sway_view_child *child,
        const struct sway_view_child_impl *impl, struct sway_view *view,
        struct wlr_surface *surface) {
    child->impl = impl;
    child->view = view;
    child->surface = surface;
    wl_list_init(&child->children);

    wl_signal_add(&surface->events.commit, &child->surface_commit);
    child->surface_commit.notify = view_child_handle_surface_commit;
    wl_signal_add(&surface->events.new_subsurface,
        &child->surface_new_subsurface);
    child->surface_new_subsurface.notify =
        view_child_handle_surface_new_subsurface;
    wl_signal_add(&surface->events.destroy, &child->surface_destroy);
    child->surface_destroy.notify = view_child_handle_surface_destroy;

    // Not all child views have a map/unmap event
    child->surface_map.notify = view_child_handle_surface_map;
    wl_list_init(&child->surface_map.link);
    child->surface_unmap.notify = view_child_handle_surface_unmap;
    wl_list_init(&child->surface_unmap.link);

    wl_signal_add(&view->events.unmap, &child->view_unmap);
    child->view_unmap.notify = view_child_handle_view_unmap;

    struct sway_output *output = child->view->window->output;
    if (output) {
        wlr_surface_send_enter(child->surface, output->wlr_output);
    }

    view_init_subsurfaces(child->view, surface);
}

void view_child_destroy(struct sway_view_child *child) {
    if (child->mapped && child->view->window != NULL) {
        view_child_damage(child, true);
    }

    if (child->parent != NULL) {
        wl_list_remove(&child->link);
        child->parent = NULL;
    }

    struct sway_view_child *subchild, *tmpchild;
    wl_list_for_each_safe(subchild, tmpchild, &child->children, link) {
        wl_list_remove(&subchild->link);
        subchild->parent = NULL;
    }

    wl_list_remove(&child->surface_commit.link);
    wl_list_remove(&child->surface_destroy.link);
    wl_list_remove(&child->surface_map.link);
    wl_list_remove(&child->surface_unmap.link);
    wl_list_remove(&child->view_unmap.link);
    wl_list_remove(&child->surface_new_subsurface.link);

    if (child->impl && child->impl->destroy) {
        child->impl->destroy(child);
    } else {
        free(child);
    }
}

static char *escape_pango_markup(const char *buffer) {
    size_t length = escape_markup_text(buffer, NULL);
    char *escaped_title = calloc(length + 1, sizeof(char));
    escape_markup_text(buffer, escaped_title);
    return escaped_title;
}

static size_t append_prop(char *buffer, const char *value) {
    if (!value) {
        return 0;
    }
    // If using pango_markup in font, we need to escape all markup chars
    // from values to make sure tags are not inserted by clients
    if (config->pango_markup) {
        char *escaped_value = escape_pango_markup(value);
        lenient_strcat(buffer, escaped_value);
        size_t len = strlen(escaped_value);
        free(escaped_value);
        return len;
    } else {
        lenient_strcat(buffer, value);
        return strlen(value);
    }
}

/**
 * Calculate and return the length of the formatted title.
 * If buffer is not NULL, also populate the buffer with the formatted title.
 */
static size_t parse_title_format(struct sway_view *view, char *buffer) {
    if (!view->title_format || strcmp(view->title_format, "%title") == 0) {
        return append_prop(buffer, view_get_title(view));
    }

    size_t len = 0;
    char *format = view->title_format;
    char *next = strchr(format, '%');
    while (next) {
        // Copy everything up to the %
        lenient_strncat(buffer, format, next - format);
        len += next - format;
        format = next;

        if (strncmp(next, "%title", 6) == 0) {
            len += append_prop(buffer, view_get_title(view));
            format += 6;
        } else if (strncmp(next, "%app_id", 7) == 0) {
            len += append_prop(buffer, view_get_app_id(view));
            format += 7;
        } else if (strncmp(next, "%class", 6) == 0) {
            len += append_prop(buffer, view_get_class(view));
            format += 6;
        } else if (strncmp(next, "%instance", 9) == 0) {
            len += append_prop(buffer, view_get_instance(view));
            format += 9;
        } else if (strncmp(next, "%shell", 6) == 0) {
            len += append_prop(buffer, view_get_shell(view));
            format += 6;
        } else {
            lenient_strcat(buffer, "%");
            ++format;
            ++len;
        }
        next = strchr(format, '%');
    }
    lenient_strcat(buffer, format);
    len += strlen(format);

    return len;
}

void view_update_title(struct sway_view *view, bool force) {
    const char *title = view_get_title(view);

    if (!force) {
        if (title && view->window->title &&
                strcmp(title, view->window->title) == 0) {
            return;
        }
        if (!title && !view->window->title) {
            return;
        }
    }

    struct window_title *title_data = view->window->data;
    free(view->window->title);
    free(title_data->formatted_title);
    if (title) {
        size_t len = parse_title_format(view, NULL);
        char *buffer = calloc(len + 1, sizeof(char));
        if (!sway_assert(buffer, "Unable to allocate title string")) {
            return;
        }
        parse_title_format(view, buffer);

        view->window->title = strdup(title);
        title_data->formatted_title = buffer;
    } else {
        view->window->title = NULL;
        title_data->formatted_title = NULL;
    }
    window_calculate_title_height(view->window);
    config_update_font_height(false);

    // Update title after the global font height is updated
    window_update_title_textures(view->window);

    if (view->foreign_toplevel && title) {
        wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title);
    }
}

void view_set_urgent(struct sway_view *view, bool enable) {
    if (view_is_urgent(view) == enable) {
        return;
    }
    if (enable) {
        struct sway_seat *seat = input_manager_current_seat();
        if (seat_get_focused_window(seat) == view->window) {
            return;
        }
        clock_gettime(CLOCK_MONOTONIC, &view->urgent);
    } else {
        view->urgent = (struct timespec){ 0 };
        if (view->urgent_timer) {
            wl_event_source_remove(view->urgent_timer);
            view->urgent_timer = NULL;
        }
    }
    window_damage_whole(view->window);
}

bool view_is_urgent(struct sway_view *view) {
    return view->urgent.tv_sec || view->urgent.tv_nsec;
}

bool view_is_transient_for(struct sway_view *child,
        struct sway_view *ancestor) {
    return child->impl->is_transient_for &&
        child->impl->is_transient_for(child, ancestor);
}
