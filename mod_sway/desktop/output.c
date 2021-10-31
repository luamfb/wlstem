#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/region.h>
#include "config.h"
#include "log.h"
#include "sway_config.h"
#include "transaction.h"
#include "input_manager.h"
#include "foreach.h"
#include "seat.h"
#include "layers.h"
#include "output.h"
#include "sway_server.h"
#include "surface.h"
#include "server_wm.h"
#include "container.h"
#include "output_manager.h"
#include "view.h"
#include "wlstem.h"
#include "server.h"

struct sway_output *output_by_name_or_id(const char *name_or_id) {
    for (int i = 0; i < wls->output_manager->outputs->length; ++i) {
        struct sway_output *output = wls->output_manager->outputs->items[i];
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        if (strcasecmp(identifier, name_or_id) == 0
                || strcasecmp(output->wlr_output->name, name_or_id) == 0) {
            return output;
        }
    }
    return NULL;
}

struct sway_output *all_output_by_name_or_id(const char *name_or_id) {
    struct sway_output *output;
    wl_list_for_each(output, &wls->output_manager->all_outputs, link) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        if (strcasecmp(identifier, name_or_id) == 0
                || strcasecmp(output->wlr_output->name, name_or_id) == 0) {
            return output;
        }
    }
    return NULL;
}

static void update_textures(struct sway_container *con, void *data) {
    container_update_title_textures(con);
}

void handle_output_commit(struct sway_output *output,
        struct wlr_output_event_commit *event) {

    if (!output->enabled) {
        return;
    }

    if (event->committed & WLR_OUTPUT_STATE_SCALE) {
        output_for_each_container(output, update_textures, NULL);
    }

    if (event->committed & (WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_SCALE)) {
        arrange_layers(output);
        arrange_output(output);
        transaction_commit_dirty();

        wls_update_output_manager_config(wls->output_manager);
    }
}

void handle_output_layout_change(struct wl_listener *listener,
        void *data) {
    struct sway_server *server =
        wl_container_of(listener, server, output_layout_change);
    wls_update_output_manager_config(wls->output_manager);
    wl_signal_emit(&wls->output_manager->events.output_layout_changed, wls->output_manager);
}

void handle_output_power_manager_set_mode(struct wl_listener *listener,
        void *data) {
    struct wlr_output_power_v1_set_mode_event *event = data;
    struct sway_output *output = event->output->data;

    struct output_config *oc = new_output_config(output->wlr_output->name);
    switch (event->mode) {
    case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
        oc->dpms_state = DPMS_OFF;
        break;
    case ZWLR_OUTPUT_POWER_V1_MODE_ON:
        oc->dpms_state = DPMS_ON;
        break;
    }
    oc = store_output_config(oc);
    apply_output_config(oc, output);
}
