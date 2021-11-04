#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "foreach.h"
#include "node.h"
#include "log.h"
#include "list.h"
#include "output.h"
#include "view.h"
#include "window.h"
#include "wlstem.h"

struct wls_window *window_create(struct sway_view *view) {
    struct wls_window *win = calloc(1, sizeof(struct wls_window));
    if (!win) {
        sway_log(SWAY_ERROR, "Unable to allocate wls_window");
        return NULL;
    }
    node_init(&win->node, N_WINDOW, win);
    win->view = view;
    win->alpha = 1.0f;

    win->outputs = create_list();

    wl_signal_init(&win->events.destroy);
    wl_signal_init(&win->events.scale_change);
    wl_signal_emit(&wls->node_manager->events.new_node, &win->node);

    // Only emit this signal when the window is fully initialized.
    wl_signal_emit(&wls->events.new_window, win);

    return win;
}

void window_destroy(struct wls_window *win) {
    if (!sway_assert(win->node.destroying,
                "Tried to free window which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(win->node.ntxnrefs == 0, "Tried to free window "
                "which is still referenced by transactions")) {
        return;
    }
    free(win->title);
    list_free(win->outputs);

    if (win->view) {
        if (win->view->window == win) {
            win->view->window = NULL;
        }
        if (win->view->destroying) {
            view_destroy(win->view);
        }
    }

    wl_signal_emit(&win->events.destroy, win);
    free(win);
}

list_t *window_get_siblings(struct wls_window *window) {
    return window->output->windows;
}

void window_detach(struct wls_window *child) {
    struct sway_output *old_output = child->output;
    list_t *siblings = window_get_siblings(child);
    if (siblings) {
        int index = list_find(siblings, child);
        if (index != -1) {
            list_del(siblings, index);
        }
    }
    child->output = NULL;

    if (old_output) {
        node_set_dirty(&old_output->node);
    }
    node_set_dirty(&child->node);
}

/**
 * Indicate to clients in this window that they are participating in (or
 * have just finished) an interactive resize
 */
void window_set_resizing(struct wls_window *win, bool resizing) {
    if (!win) {
        return;
    }

    if (win->view) {
        if (win->view->impl->set_resizing) {
            win->view->impl->set_resizing(win->view, resizing);
        }
    }
}

void window_get_box(struct wls_window *window, struct wlr_box *box) {
    box->x = window->x;
    box->y = window->y;
    box->width = window->width;
    box->height = window->height;
}

int window_sibling_index(struct wls_window *child) {
    return list_find(window_get_siblings(child), child);
}

list_t *window_get_current_siblings(struct wls_window *window) {
    struct sway_output *current_output = window->current.output;
    if (!current_output) {
        sway_log(SWAY_ERROR, "window has no current output!");
        assert(false);
    }
    return current_output->current.windows;
}

void window_add_sibling(struct wls_window *fixed,
        struct wls_window *active, bool after) {
    if (active->output) {
        window_detach(active);
    }
    list_t *siblings = window_get_siblings(fixed);
    int index = list_find(siblings, fixed);
    list_insert(siblings, index + after, active);
    active->output = fixed->output;
}

void window_replace(struct wls_window *window,
        struct wls_window *replacement) {
    if (window->output) {
        window_add_sibling(window, replacement, 1);
        window_detach(window);
    }
}
/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct sway_output *window_get_effective_output(struct wls_window *win) {
    if (win->outputs->length == 0) {
        return NULL;
    }
    return win->outputs->items[win->outputs->length - 1];
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
        int x, int y, void *data) {
    struct wlr_output *wlr_output = data;
    wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
        int x, int y, void *data) {
    struct wlr_output *wlr_output = data;
    wlr_surface_send_leave(surface, wlr_output);
}

void window_discover_outputs(struct wls_window *win) {
    struct wlr_box con_box = {
        .x = win->current.x,
        .y = win->current.y,
        .width = win->current.width,
        .height = win->current.height,
    };
    struct sway_output *old_output = window_get_effective_output(win);

    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        struct wlr_box output_box;
        output_get_box(output, &output_box);
        struct wlr_box intersection;
        bool intersects =
            wlr_box_intersection(&intersection, &con_box, &output_box);
        int index = list_find(win->outputs, output);

        if (intersects && index == -1) {
            // Send enter
            sway_log(SWAY_DEBUG, "Window %p entered output %p", win, output);
            if (win->view) {
                view_for_each_surface(win->view,
                        surface_send_enter_iterator, output->wlr_output);
                if (win->view->foreign_toplevel) {
                    wlr_foreign_toplevel_handle_v1_output_enter(
                            win->view->foreign_toplevel, output->wlr_output);
                }
            }
            list_add(win->outputs, output);
        } else if (!intersects && index != -1) {
            // Send leave
            sway_log(SWAY_DEBUG, "Window %p left output %p", win, output);
            if (win->view) {
                view_for_each_surface(win->view,
                    surface_send_leave_iterator, output->wlr_output);
                if (win->view->foreign_toplevel) {
                    wlr_foreign_toplevel_handle_v1_output_leave(
                            win->view->foreign_toplevel, output->wlr_output);
                }
            }
            list_del(win->outputs, index);
        }
    }
    struct sway_output *new_output = window_get_effective_output(win);
    double old_scale = old_output && old_output->enabled ?
        old_output->wlr_output->scale : -1;
    double new_scale = new_output ? new_output->wlr_output->scale : -1;
    if (old_scale != new_scale) {
        wl_signal_emit(&win->events.scale_change, win);
    }
}
