#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include "output.h"
#include "container.h"
#include "root.h"
#include "list.h"
#include "log.h"
#include "util.h"
#include "wlstem.h"
#include "wls_server.h"

void update_output_manager_config(struct sway_root *root) {
    struct wlr_output_configuration_v1 *config =
        wlr_output_configuration_v1_create();

    struct sway_output *output;
    wl_list_for_each(output, &root->all_outputs, link) {
        if (output == root->noop_output) {
            continue;
        }
        struct wlr_output_configuration_head_v1 *config_head =
            wlr_output_configuration_head_v1_create(config, output->wlr_output);
        struct wlr_box *output_box = wlr_output_layout_get_box(
            root->output_layout, output->wlr_output);
        // We mark the output enabled even if it is switched off by DPMS
        config_head->state.enabled = output->enabled;
        config_head->state.mode = output->current_mode;
        if (output_box) {
            config_head->state.x = output_box->x;
            config_head->state.y = output_box->y;
        }
    }

    wlr_output_manager_v1_set_configuration(root->output_manager_v1, config);
}

static void output_manager_apply(struct sway_root *root,
        struct wlr_output_configuration_v1 *config, bool test_only) {
    // TODO: perform atomic tests on the whole backend atomically

    struct wlr_output_configuration_head_v1 *config_head;
    // First disable outputs we need to disable
    bool ok = true;
    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output *wlr_output = config_head->state.output;
        struct sway_output *output = wlr_output->data;
        if (!output->enabled || config_head->state.enabled) {
            continue;
        }
        struct output_config *oc = new_output_config(output->wlr_output->name);
        oc->enabled = false;

        if (test_only) {
            ok &= test_output_config(oc, output);
        } else {
            oc = store_output_config(oc);
            ok &= apply_output_config(oc, output);
        }
    }

    // Then enable outputs that need to
    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output *wlr_output = config_head->state.output;
        struct sway_output *output = wlr_output->data;
        if (!config_head->state.enabled) {
            continue;
        }
        struct output_config *oc = new_output_config(output->wlr_output->name);
        oc->enabled = true;
        if (config_head->state.mode != NULL) {
            struct wlr_output_mode *mode = config_head->state.mode;
            oc->width = mode->width;
            oc->height = mode->height;
            oc->refresh_rate = mode->refresh / 1000.f;
        } else {
            oc->width = config_head->state.custom_mode.width;
            oc->height = config_head->state.custom_mode.height;
            oc->refresh_rate =
                config_head->state.custom_mode.refresh / 1000.f;
        }
        oc->x = config_head->state.x;
        oc->y = config_head->state.y;
        oc->transform = config_head->state.transform;
        oc->scale = config_head->state.scale;

        if (test_only) {
            ok &= test_output_config(oc, output);
        } else {
            oc = store_output_config(oc);
            ok &= apply_output_config(oc, output);
        }
    }

    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);

    if (!test_only) {
        update_output_manager_config(wls->root);
    }
}

static void handle_output_manager_apply(struct wl_listener *listener, void *data) {
    struct sway_root *root =
        wl_container_of(listener, root, output_manager_apply);
    struct wlr_output_configuration_v1 *config = data;

    output_manager_apply(root, config, false);
}

static void handle_output_manager_test(struct wl_listener *listener, void *data) {
    struct sway_root *root =
        wl_container_of(listener, root, output_manager_test);
    struct wlr_output_configuration_v1 *config = data;

    output_manager_apply(root, config, true);
}

struct sway_root *root_create(struct wls_server *server) {
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

    root->output_manager_v1 =
        wlr_output_manager_v1_create(server->wl_display);
    root->output_manager_apply.notify = handle_output_manager_apply;
    wl_signal_add(&root->output_manager_v1->events.apply,
        &root->output_manager_apply);
    root->output_manager_test.notify = handle_output_manager_test;
    wl_signal_add(&root->output_manager_v1->events.test,
        &root->output_manager_test);

    wl_signal_init(&root->events.output_layout_changed);
    wl_signal_init(&root->events.output_connected);
    wl_signal_init(&root->events.output_disconnected);

    return root;
}

void root_destroy(struct sway_root *root) {
    list_free(root->outputs);
    wlr_output_layout_destroy(root->output_layout);
    free(root);
}

void root_for_each_output(void (*f)(struct sway_output *output, void *data),
        void *data) {
    for (int i = 0; i < wls->root->outputs->length; ++i) {
        struct sway_output *output = wls->root->outputs->items[i];
        f(output, data);
    }
}

void root_get_box(struct sway_root *root, struct wlr_box *box) {
    box->x = root->x;
    box->y = root->y;
    box->width = root->width;
    box->height = root->height;
}
