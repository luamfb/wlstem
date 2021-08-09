#ifndef _SWAY_WORKSPACE_H
#define _SWAY_WORKSPACE_H

#include <stdbool.h>
#include "sway/tree/container.h"
#include "sway/tree/node.h"

struct sway_workspace {
    struct sway_output *output; // NULL if no outputs are connected
};

struct sway_workspace *workspace_create(struct sway_output *output);

void workspace_begin_destroy(struct sway_workspace *workspace);

bool workspace_is_visible(struct sway_workspace *ws);

void workspace_detach(struct sway_workspace *workspace);

#endif
