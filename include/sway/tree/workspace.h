#ifndef _SWAY_WORKSPACE_H
#define _SWAY_WORKSPACE_H

#include <stdbool.h>
#include "sway/tree/container.h"
#include "sway/tree/node.h"

struct sway_view;

struct sway_workspace_state {
    struct sway_output *output;

    bool focused;
};

struct sway_workspace {
    struct sway_output *output; // NULL if no outputs are connected

    struct sway_workspace_state current;
};

struct sway_workspace *workspace_create(struct sway_output *output);

void workspace_destroy(struct sway_workspace *workspace);

void workspace_begin_destroy(struct sway_workspace *workspace);

bool workspace_is_visible(struct sway_workspace *ws);

bool workspace_is_empty(struct sway_workspace *ws);

void workspace_for_each_container(struct sway_workspace *ws,
        void (*f)(struct sway_container *con, void *data), void *data);

void workspace_detach(struct sway_workspace *workspace);

#endif
