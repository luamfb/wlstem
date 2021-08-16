#ifndef _SWAY_ARRANGE_H
#define _SWAY_ARRANGE_H

struct sway_output;
struct sway_container;

void arrange_container(struct sway_container *container);

void arrange_output(struct sway_output *output);

void arrange_root(void);

void arrange_output_layout(void);

#endif
