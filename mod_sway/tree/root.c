#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/desktop/transaction.h"
#include "sway/input/seat.h"
#include "output.h"
#include "sway/tree/arrange.h"
#include "container.h"
#include "root.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct sway_root *root;

static void output_layout_handle_change(struct wl_listener *listener,
        void *data) {
    arrange_root();
    transaction_commit_dirty();
}

struct sway_root *root_create(void) {
    struct sway_root *root = calloc(1, sizeof(struct sway_root));
    if (!root) {
        sway_log(SWAY_ERROR, "Unable to allocate sway_root");
        return NULL;
    }
    root->output_layout = wlr_output_layout_create();
    wl_list_init(&root->all_outputs);
#if HAVE_XWAYLAND
    wl_list_init(&root->xwayland_unmanaged);
#endif
    wl_list_init(&root->drag_icons);
    root->outputs = create_list();

    root->output_layout_change.notify = output_layout_handle_change;
    wl_signal_add(&root->output_layout->events.change,
        &root->output_layout_change);
    return root;
}

void root_destroy(struct sway_root *root) {
    wl_list_remove(&root->output_layout_change.link);
    list_free(root->outputs);
    wlr_output_layout_destroy(root->output_layout);
    free(root);
}

void root_for_each_output(void (*f)(struct sway_output *output, void *data),
        void *data) {
    for (int i = 0; i < root->outputs->length; ++i) {
        struct sway_output *output = root->outputs->items[i];
        f(output, data);
    }
}

void root_for_each_container(void (*f)(struct sway_container *con, void *data),
        void *data) {
    for (int i = 0; i < root->outputs->length; ++i) {
        struct sway_output *output = root->outputs->items[i];
        output_for_each_container(output, f, data);
    }

    if (root->noop_output->active) {
        output_for_each_container(root->noop_output, f, data);
    }
}

void root_get_box(struct sway_root *root, struct wlr_box *box) {
    box->x = root->x;
    box->y = root->y;
    box->width = root->width;
    box->height = root->height;
}
