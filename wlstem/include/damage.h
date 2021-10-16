#ifndef WLSTEM_DAMAGE_H_
#define WLSTEM_DAMAGE_H_

#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>

struct sway_container;
struct sway_output;
struct sway_view;

void view_damage_from(struct sway_view *view);

void container_damage_whole(struct sway_container *container);

void output_damage_whole(struct sway_output *output);

void output_damage_surface(struct sway_output *output, double ox, double oy,
    struct wlr_surface *surface, bool whole);

void output_damage_from_view(struct sway_output *output,
    struct sway_view *view);

void output_damage_box(struct sway_output *output, struct wlr_box *box);

void output_damage_whole_container(struct sway_output *output,
    struct sway_container *con);

void desktop_damage_surface(struct wlr_surface *surface, double lx, double ly,
    bool whole);

void desktop_damage_whole_container(struct sway_container *con);

void desktop_damage_box(struct wlr_box *box);

void desktop_damage_view(struct sway_view *view);

#endif /* WLSTEM_DAMAGE_H_ */
