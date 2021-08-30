#define _POSIX_C_SOURCE 200809L

#include <wayland-server-core.h>
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
