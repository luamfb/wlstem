#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output_damage.h>
#include "list.h"
#include "log.h"
#include "output.h"
#include "wlstem.h"

void output_enable(struct sway_output *output) {
    if (!sway_assert(!output->enabled, "output is already enabled")) {
        return;
    }
    output->tiling = create_list();
    output->enabled = true;
    list_add(wls->output_manager->outputs, output);

    if (!output->active) {
        sway_log(SWAY_DEBUG, "Activating output '%s'", output->wlr_output->name);
        output->active = true;
    }

    wl_signal_emit(&wls->node_manager->events.new_node, &output->node);
    wl_signal_emit(&wls->output_manager->events.output_connected, output);
}

void output_destroy(struct sway_output *output) {
    if (!sway_assert(output->node.destroying,
                "Tried to free output which wasn't marked as destroying")) {
        return;
    }
    if (!sway_assert(output->wlr_output == NULL,
                "Tried to free output which still had a wlr_output")) {
        return;
    }
    if (!sway_assert(output->node.ntxnrefs == 0, "Tried to free output "
                "which is still referenced by transactions")) {
        return;
    }
    wl_event_source_remove(output->repaint_timer);
    list_free(output->tiling);
    list_free(output->current.tiling);
    free(output);
}

void output_damage_whole(struct sway_output *output) {
    // The output can exist with no wlr_output if it's just been disconnected
    // and the transaction to evacuate it has't completed yet.
    if (output && output->wlr_output && output->damage) {
        wlr_output_damage_add_whole(output->damage);
    }
}
