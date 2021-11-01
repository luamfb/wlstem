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
#include "window_title.h"
#include "server.h"

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
