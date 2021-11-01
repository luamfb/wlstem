#define _POSIX_C_SOURCE 200809L
#include <wlr/types/wlr_output.h>
#include "foreach.h"
#include "output.h"
#include "server_arrange.h"
#include "transaction.h"
#include "window_title.h"
#include "wlstem.h"

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

struct sway_output * choose_absorber_output(struct sway_output *giver) {
    struct sway_output *absorber = NULL;
    if (wls->output_manager->outputs->length > 1) {
        absorber = wls->output_manager->outputs->items[0];
        if (absorber == giver) {
            absorber = wls->output_manager->outputs->items[1];
        }
    }
    return absorber;
}

