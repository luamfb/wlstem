#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include "sway_config.h"
#include "sway_desktop.h"
#include "sway_transaction.h"
#include "idle_inhibit_v1.h"
#include "input_manager.h"
#include "cursor.h"
#include "output.h"
#include "container.h"
#include "node.h"
#include "view.h"
#include "list.h"
#include "log.h"
#include "wlstem.h"
#include "server.h"

#define DEFAULT_TRANSACTION_TIMEOUT_MS 200

struct sway_transaction {
    struct wl_event_source *timer;
    list_t *instructions;   // struct sway_transaction_instruction *
    size_t num_waiting;
    size_t num_configures;
    struct timespec commit_time;
};

struct sway_transaction_instruction {
    struct sway_transaction *transaction;
    struct wls_transaction_node *node;
    union {
        struct sway_output_state output_state;
        struct sway_container_state container_state;
    };
    uint32_t serial;
};

static struct sway_transaction *transaction_create(void) {
    struct sway_transaction *transaction =
        calloc(1, sizeof(struct sway_transaction));
    if (!sway_assert(transaction, "Unable to allocate transaction")) {
        return NULL;
    }
    transaction->instructions = create_list();
    return transaction;
}

static void transaction_destroy(struct sway_transaction *transaction) {
    // Free instructions
    for (int i = 0; i < transaction->instructions->length; ++i) {
        struct sway_transaction_instruction *instruction =
            transaction->instructions->items[i];
        struct wls_transaction_node *node = instruction->node;
        node->ntxnrefs--;
        if (node->instruction == instruction) {
            node->instruction = NULL;
        }
        if (node->destroying && node->ntxnrefs == 0) {
            switch (node->type) {
            case N_OUTPUT:
                output_destroy(node->sway_output);
                break;
            case N_CONTAINER:
                container_destroy(node->sway_container);
                break;
            }
        }
        free(instruction);
    }
    list_free(transaction->instructions);

    if (transaction->timer) {
        wl_event_source_remove(transaction->timer);
    }
    free(transaction);
}

static void copy_output_state(struct sway_output *output,
        struct sway_transaction_instruction *instruction) {
    struct sway_output_state *state = &instruction->output_state;

    state->tiling = create_list();
    list_cat(state->tiling, output->tiling);
    state->active = output->active;
}

static void copy_container_state(struct sway_container *container,
        struct sway_transaction_instruction *instruction) {
    struct sway_container_state *state = &instruction->container_state;

    state->x = container->x;
    state->y = container->y;
    state->width = container->width;
    state->height = container->height;
    state->output = container->output;
    state->border_thickness = container->border_thickness;
    state->border_top = container->border_top;
    state->border_left = container->border_left;
    state->border_right = container->border_right;
    state->border_bottom = container->border_bottom;
    state->content_x = container->content_x;
    state->content_y = container->content_y;
    state->content_width = container->content_width;
    state->content_height = container->content_height;

    struct sway_seat *seat = input_manager_current_seat();
    state->focused = seat_get_focus(seat) == &container->node;
}

static void transaction_add_node(struct sway_transaction *transaction,
        struct wls_transaction_node *node) {
    struct sway_transaction_instruction *instruction =
        calloc(1, sizeof(struct sway_transaction_instruction));
    if (!sway_assert(instruction, "Unable to allocate instruction")) {
        return;
    }
    instruction->transaction = transaction;
    instruction->node = node;

    switch (node->type) {
    case N_OUTPUT:
        copy_output_state(node->sway_output, instruction);
        break;
    case N_CONTAINER:
        copy_container_state(node->sway_container, instruction);
        break;
    }

    list_add(transaction->instructions, instruction);
    node->ntxnrefs++;
}

static void apply_output_state(struct sway_output *output,
        struct sway_output_state *state) {
    output_damage_whole(output);
    list_free(output->current.tiling);
    memcpy(&output->current, state, sizeof(struct sway_output_state));
    output_damage_whole(output);
}

static void apply_container_state(struct sway_container *container,
        struct sway_container_state *state) {
    struct sway_view *view = container->view;
    // Damage the old location
    desktop_damage_whole_container(container);
    if (view && !wl_list_empty(&view->saved_buffers)) {
        struct sway_saved_buffer *saved_buf;
        wl_list_for_each(saved_buf, &view->saved_buffers, link) {
            struct wlr_box box = {
                .x = container->current.content_x - view->saved_geometry.x + saved_buf->x,
                .y = container->current.content_y - view->saved_geometry.y + saved_buf->y,
                .width = saved_buf->width,
                .height = saved_buf->height,
            };
            desktop_damage_box(&box);
        }
    }

    memcpy(&container->current, state, sizeof(struct sway_container_state));

    if (view && !wl_list_empty(&view->saved_buffers)) {
        if (!container->node.destroying || container->node.ntxnrefs == 1) {
            view_remove_saved_buffer(view);
        }
    }

    // Damage the new location
    desktop_damage_whole_container(container);
    if (view && view->surface) {
        struct wlr_surface *surface = view->surface;
        struct wlr_box box = {
            .x = container->current.content_x - view->geometry.x,
            .y = container->current.content_y - view->geometry.y,
            .width = surface->current.width,
            .height = surface->current.height,
        };
        desktop_damage_box(&box);
    }

    // If the view hasn't responded to the configure, center it within
    // the container. This is important for fullscreen views which
    // refuse to resize to the size of the output.
    if (view && view->surface) {
        if (view->geometry.width < container->current.content_width) {
            container->surface_x = container->current.content_x +
                (container->current.content_width - view->geometry.width) / 2;
        } else {
            container->surface_x = container->current.content_x;
        }
        if (view->geometry.height < container->current.content_height) {
            container->surface_y = container->current.content_y +
                (container->current.content_height - view->geometry.height) / 2;
        } else {
            container->surface_y = container->current.content_y;
        }
    }

    if (!container->node.destroying) {
        container_discover_outputs(container);
    }
}

/**
 * Apply a transaction to the "current" state of the tree.
 */
static void transaction_apply(struct sway_transaction *transaction) {
    sway_log(SWAY_DEBUG, "Applying transaction %p", transaction);
    if (wls->debug.txn_timings) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        struct timespec *commit = &transaction->commit_time;
        float ms = (now.tv_sec - commit->tv_sec) * 1000 +
            (now.tv_nsec - commit->tv_nsec) / 1000000.0;
        sway_log(SWAY_DEBUG, "Transaction %p: %.1fms waiting "
                "(%.1f frames if 60Hz)", transaction, ms, ms / (1000.0f / 60));
    }

    // Apply the instruction state to the node's current state
    for (int i = 0; i < transaction->instructions->length; ++i) {
        struct sway_transaction_instruction *instruction =
            transaction->instructions->items[i];
        struct wls_transaction_node *node = instruction->node;

        switch (node->type) {
        case N_OUTPUT:
            apply_output_state(node->sway_output, &instruction->output_state);
            break;
        case N_CONTAINER:
            apply_container_state(node->sway_container,
                    &instruction->container_state);
            break;
        }

        node->instruction = NULL;
    }

    cursor_rebase_all();
}

static void transaction_commit(struct sway_transaction *transaction);

// Return true if both transactions operate on the same nodes
static bool transaction_same_nodes(struct sway_transaction *a,
        struct sway_transaction *b) {
    if (a->instructions->length != b->instructions->length) {
        return false;
    }
    for (int i = 0; i < a->instructions->length; ++i) {
        struct sway_transaction_instruction *a_inst = a->instructions->items[i];
        struct sway_transaction_instruction *b_inst = b->instructions->items[i];
        if (a_inst->node != b_inst->node) {
            return false;
        }
    }
    return true;
}

static void transaction_progress_queue(void) {
    if (!wls->node_manager->transactions->length) {
        return;
    }
    // Only the first transaction in the queue is committed, so that's the one
    // we try to process.
    struct sway_transaction *transaction = wls->node_manager->transactions->items[0];
    if (transaction->num_waiting) {
        return;
    }
    transaction_apply(transaction);
    transaction_destroy(transaction);
    list_del(wls->node_manager->transactions, 0);

    if (wls->node_manager->transactions->length == 0) {
        // The transaction queue is empty, so we're done.
        sway_idle_inhibit_v1_check_active(wls->misc_protocols->idle_inhibit_manager_v1);
        return;
    }

    // If there's a bunch of consecutive transactions which all apply to the
    // same views, skip all except the last one.
    while (wls->node_manager->transactions->length >= 2) {
        struct sway_transaction *txn = wls->node_manager->transactions->items[0];
        struct sway_transaction *dup = NULL;

        for (int i = 1; i < wls->node_manager->transactions->length; i++) {
            struct sway_transaction *maybe_dup = wls->node_manager->transactions->items[i];
            if (transaction_same_nodes(txn, maybe_dup)) {
                dup = maybe_dup;
                break;
            }
        }

        if (dup) {
            list_del(wls->node_manager->transactions, 0);
            transaction_destroy(txn);
        } else {
            break;
        }
    }

    // We again commit the first transaction in the queue to process it.
    transaction = wls->node_manager->transactions->items[0];
    transaction_commit(transaction);
    transaction_progress_queue();
}

static int handle_timeout(void *data) {
    struct sway_transaction *transaction = data;
    sway_log(SWAY_DEBUG, "Transaction %p timed out (%zi waiting)",
            transaction, transaction->num_waiting);
    transaction->num_waiting = 0;
    transaction_progress_queue();
    return 0;
}

static bool should_configure(struct wls_transaction_node *node,
        struct sway_transaction_instruction *instruction) {
    if (!node_is_view(node)) {
        return false;
    }
    if (node->destroying) {
        return false;
    }
    struct sway_container_state *cstate = &node->sway_container->current;
    struct sway_container_state *istate = &instruction->container_state;
#if HAVE_XWAYLAND
    // Xwayland views are position-aware and need to be reconfigured
    // when their position changes.
    if (node->sway_container->view->type == SWAY_VIEW_XWAYLAND) {
        // Sway logical coordinates are doubles, but they get truncated to
        // integers when sent to Xwayland through `xcb_configure_window`.
        // X11 apps will not respond to duplicate configure requests (from their
        // truncated point of view) and cause transactions to time out.
        if ((int)cstate->content_x != (int)istate->content_x ||
                (int)cstate->content_y != (int)istate->content_y) {
            return true;
        }
    }
#endif
    if (cstate->content_width == istate->content_width &&
            cstate->content_height == istate->content_height) {
        return false;
    }
    return true;
}

static void transaction_commit(struct sway_transaction *transaction) {
    sway_log(SWAY_DEBUG, "Transaction %p committing with %i instructions",
            transaction, transaction->instructions->length);
    transaction->num_waiting = 0;
    for (int i = 0; i < transaction->instructions->length; ++i) {
        struct sway_transaction_instruction *instruction =
            transaction->instructions->items[i];
        struct wls_transaction_node *node = instruction->node;
        if (should_configure(node, instruction)) {
            instruction->serial = view_configure(node->sway_container->view,
                    instruction->container_state.content_x,
                    instruction->container_state.content_y,
                    instruction->container_state.content_width,
                    instruction->container_state.content_height);
            ++transaction->num_waiting;

            // From here on we are rendering a saved buffer of the view, which
            // means we can send a frame done event to make the client redraw it
            // as soon as possible. Additionally, this is required if a view is
            // mapping and its default geometry doesn't intersect an output.
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            wlr_surface_send_frame_done(
                    node->sway_container->view->surface, &now);
        }
        if (node_is_view(node) && wl_list_empty(&node->sway_container->view->saved_buffers)) {
            view_save_buffer(node->sway_container->view);
            memcpy(&node->sway_container->view->saved_geometry,
                    &node->sway_container->view->geometry,
                    sizeof(struct wlr_box));
        }
        node->instruction = instruction;
    }
    transaction->num_configures = transaction->num_waiting;
    if (wls->debug.txn_timings) {
        clock_gettime(CLOCK_MONOTONIC, &transaction->commit_time);
    }
    if (wls->debug.noatomic) {
        transaction->num_waiting = 0;
    } else if (wls->debug.txn_wait) {
        // Force the transaction to time out even if all views are ready.
        // We do this by inflating the waiting counter.
        transaction->num_waiting += 1000000;
    }

    if (transaction->num_waiting) {
        // Set up a timer which the views must respond within
        transaction->timer = wl_event_loop_add_timer(wls->server->wl_event_loop,
                handle_timeout, transaction);
        if (transaction->timer) {
            size_t timeout = wls->debug.transaction_timeout_ms;
            if (!timeout) {
                timeout = wls->debug.transaction_timeout_ms =
                    DEFAULT_TRANSACTION_TIMEOUT_MS;
            }
            wl_event_source_timer_update(transaction->timer, timeout);
        } else {
            sway_log_errno(SWAY_ERROR, "Unable to create transaction timer "
                    "(some imperfect frames might be rendered)");
            transaction->num_waiting = 0;
        }
    }
}

static void set_instruction_ready(
        struct sway_transaction_instruction *instruction) {
    struct sway_transaction *transaction = instruction->transaction;

    if (wls->debug.txn_timings) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        struct timespec *start = &transaction->commit_time;
        float ms = (now.tv_sec - start->tv_sec) * 1000 +
            (now.tv_nsec - start->tv_nsec) / 1000000.0;
        sway_log(SWAY_DEBUG, "Transaction %p: %zi/%zi ready in %.1fms (%s)",
                transaction,
                transaction->num_configures - transaction->num_waiting + 1,
                transaction->num_configures, ms,
                instruction->node->sway_container->title);
    }

    // If the transaction has timed out then its num_waiting will be 0 already.
    if (transaction->num_waiting > 0 && --transaction->num_waiting == 0) {
        sway_log(SWAY_DEBUG, "Transaction %p is ready", transaction);
        wl_event_source_timer_update(transaction->timer, 0);
    }

    instruction->node->instruction = NULL;
    transaction_progress_queue();
}

void transaction_notify_view_ready_by_serial(struct sway_view *view,
        uint32_t serial) {
    struct sway_transaction_instruction *instruction =
        view->container->node.instruction;
    if (instruction != NULL && instruction->serial == serial) {
        set_instruction_ready(instruction);
    }
}

void transaction_notify_view_ready_by_geometry(struct sway_view *view,
        double x, double y, int width, int height) {
    struct sway_transaction_instruction *instruction =
        view->container->node.instruction;
    if (instruction != NULL &&
            (int)instruction->container_state.content_x == (int)x &&
            (int)instruction->container_state.content_y == (int)y &&
            instruction->container_state.content_width == width &&
            instruction->container_state.content_height == height) {
        set_instruction_ready(instruction);
    }
}

void transaction_notify_view_ready_immediately(struct sway_view *view) {
    struct sway_transaction_instruction *instruction =
            view->container->node.instruction;
    if (instruction != NULL) {
        set_instruction_ready(instruction);
    }
}

void transaction_commit_dirty(void) {
    list_t *dirty_nodes = wls->node_manager->dirty_nodes;
    if (!dirty_nodes) {
        return;
    }
    struct sway_transaction *transaction = transaction_create();
    if (!transaction) {
        return;
    }
    for (int i = 0; i < dirty_nodes->length; ++i) {
        struct wls_transaction_node *node = dirty_nodes->items[i];
        transaction_add_node(transaction, node);
        node->dirty = false;
    }
    dirty_nodes->length = 0;

    list_add(wls->node_manager->transactions, transaction);

    // We only commit the first transaction added to the queue.
    if (wls->node_manager->transactions->length == 1) {
        transaction_commit(transaction);
        // Attempting to progress the queue here is useful
        // if the transaction has nothing to wait for.
        transaction_progress_queue();
    }
}

#undef DEFAULT_TRANSACTION_TIMEOUT_MS
