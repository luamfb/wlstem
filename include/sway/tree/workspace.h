#ifndef _SWAY_WORKSPACE_H
#define _SWAY_WORKSPACE_H

#include <stdbool.h>
#include "sway/tree/container.h"
#include "sway/tree/node.h"

struct sway_view;

struct sway_workspace_state {
    double x, y;
    int width, height;
    struct sway_output *output;
    list_t *tiling;

    bool focused;
};

struct sway_workspace {
    struct sway_node node;

    char *name;

    double x, y;
    int width, height;

    struct sway_output *output; // NULL if no outputs are connected
    list_t *tiling;             // struct sway_container

    struct sway_workspace_state current;
};

struct sway_workspace *workspace_create(struct sway_output *output,
        const char *name);

void workspace_destroy(struct sway_workspace *workspace);

void workspace_begin_destroy(struct sway_workspace *workspace);

void workspace_consider_destroy(struct sway_workspace *ws);

char *workspace_next_name(const char *output_name);

struct sway_workspace *workspace_by_number(const char* name);

bool workspace_is_visible(struct sway_workspace *ws);

bool workspace_is_empty(struct sway_workspace *ws);

void workspace_for_each_container(struct sway_workspace *ws,
        void (*f)(struct sway_container *con, void *data), void *data);

struct sway_container *workspace_find_container(struct sway_workspace *ws,
        bool (*test)(struct sway_container *con, void *data), void *data);

void workspace_detach(struct sway_workspace *workspace);

struct sway_container *workspace_add_tiling(struct sway_workspace *workspace,
        struct sway_container *con);

void workspace_get_box(struct sway_workspace *workspace, struct wlr_box *box);

#endif
