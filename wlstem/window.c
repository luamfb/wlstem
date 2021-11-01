#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "container.h"
#include "foreach.h"
#include "node.h"
#include "log.h"
#include "list.h"
#include "output.h"
#include "view.h"
#include "wlstem.h"

struct sway_container *container_create(struct sway_view *view) {
    struct sway_container *c = calloc(1, sizeof(struct sway_container));
    if (!c) {
        sway_log(SWAY_ERROR, "Unable to allocate sway_container");
        return NULL;
    }
    node_init(&c->node, N_CONTAINER, c);
    c->view = view;
    c->alpha = 1.0f;

    c->outputs = create_list();

    wl_signal_init(&c->events.destroy);
    wl_signal_init(&c->events.scale_change);
    wl_signal_emit(&wls->node_manager->events.new_node, &c->node);

    // Only emit this signal when the container is fully initialized.
    wl_signal_emit(&wls->events.new_window, c);

    return c;
}

void container_destroy(struct sway_container *con) {
    if (!sway_assert(con->node.destroying,
                "Tried to free container which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(con->node.ntxnrefs == 0, "Tried to free container "
                "which is still referenced by transactions")) {
        return;
    }
    free(con->title);
    list_free(con->outputs);

    if (con->view) {
        if (con->view->container == con) {
            con->view->container = NULL;
        }
        if (con->view->destroying) {
            view_destroy(con->view);
        }
    }

    wl_signal_emit(&con->events.destroy, con);
    free(con);
}

list_t *container_get_siblings(struct sway_container *container) {
    return container->output->windows;
}

void container_detach(struct sway_container *child) {
    struct sway_output *old_output = child->output;
    list_t *siblings = container_get_siblings(child);
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
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void container_set_resizing(struct sway_container *con, bool resizing) {
    if (!con) {
        return;
    }

    if (con->view) {
        if (con->view->impl->set_resizing) {
            con->view->impl->set_resizing(con->view, resizing);
        }
    }
}

void container_get_box(struct sway_container *container, struct wlr_box *box) {
    box->x = container->x;
    box->y = container->y;
    box->width = container->width;
    box->height = container->height;
}

int container_sibling_index(struct sway_container *child) {
    return list_find(container_get_siblings(child), child);
}

list_t *container_get_current_siblings(struct sway_container *container) {
    struct sway_output *current_output = container->current.output;
    if (!current_output) {
        sway_log(SWAY_ERROR, "container has no current output!");
        assert(false);
    }
    return current_output->current.windows;
}

void container_add_sibling(struct sway_container *fixed,
        struct sway_container *active, bool after) {
    if (active->output) {
        container_detach(active);
    }
    list_t *siblings = container_get_siblings(fixed);
    int index = list_find(siblings, fixed);
    list_insert(siblings, index + after, active);
    active->output = fixed->output;
}

void container_replace(struct sway_container *container,
        struct sway_container *replacement) {
    if (container->output) {
        container_add_sibling(container, replacement, 1);
        container_detach(container);
    }
}
/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct sway_output *container_get_effective_output(struct sway_container *con) {
    if (con->outputs->length == 0) {
        return NULL;
    }
    return con->outputs->items[con->outputs->length - 1];
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

void container_discover_outputs(struct sway_container *con) {
    struct wlr_box con_box = {
        .x = con->current.x,
        .y = con->current.y,
        .width = con->current.width,
        .height = con->current.height,
    };
    struct sway_output *old_output = container_get_effective_output(con);

    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        struct wlr_box output_box;
        output_get_box(output, &output_box);
        struct wlr_box intersection;
        bool intersects =
            wlr_box_intersection(&intersection, &con_box, &output_box);
        int index = list_find(con->outputs, output);

        if (intersects && index == -1) {
            // Send enter
            sway_log(SWAY_DEBUG, "Container %p entered output %p", con, output);
            if (con->view) {
                view_for_each_surface(con->view,
                        surface_send_enter_iterator, output->wlr_output);
                if (con->view->foreign_toplevel) {
                    wlr_foreign_toplevel_handle_v1_output_enter(
                            con->view->foreign_toplevel, output->wlr_output);
                }
            }
            list_add(con->outputs, output);
        } else if (!intersects && index != -1) {
            // Send leave
            sway_log(SWAY_DEBUG, "Container %p left output %p", con, output);
            if (con->view) {
                view_for_each_surface(con->view,
                    surface_send_leave_iterator, output->wlr_output);
                if (con->view->foreign_toplevel) {
                    wlr_foreign_toplevel_handle_v1_output_leave(
                            con->view->foreign_toplevel, output->wlr_output);
                }
            }
            list_del(con->outputs, index);
        }
    }
    struct sway_output *new_output = container_get_effective_output(con);
    double old_scale = old_output && old_output->enabled ?
        old_output->wlr_output->scale : -1;
    double new_scale = new_output ? new_output->wlr_output->scale : -1;
    if (old_scale != new_scale) {
        wl_signal_emit(&con->events.scale_change, con);
    }
}
