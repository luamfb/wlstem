#ifndef _SWAY_OUTPUT_H
#define _SWAY_OUTPUT_H
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "config.h"
#include "output_config.h"
#include "node.h"

struct wls_window;
struct sway_view;

struct sway_output_state {
    bool active;
    list_t *windows;             // struct wls_window
    int render_lx, render_ly; // in layout coords
};

struct sway_output {
    struct wls_transaction_node node;
    struct wlr_output *wlr_output;
    struct wl_list link;

    struct wl_list layers[4]; // sway_layer_surface::link
    struct wlr_box usable_area;

    struct timespec last_frame;
    struct wlr_output_damage *damage;

    list_t *windows;             // struct wls_window

    int lx, ly; // layout coords
    int render_lx, render_ly; // in layout coords
    int width, height; // transformed buffer size
    enum wl_output_subpixel detected_subpixel;
    enum scale_filter_mode scale_filter;
    // last applied mode when the output is DPMS'ed
    struct wlr_output_mode *current_mode;

    bool enabling, enabled;
    bool active;

    struct sway_output_state current;

    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener mode;
    struct wl_listener present;
    struct wl_listener damage_destroy;
    struct wl_listener damage_frame;

    struct {
        struct wl_signal destroy;
    } events;

    struct timespec last_presentation;
    uint32_t refresh_nsec;
    int max_render_time; // In milliseconds
    struct wl_event_source *repaint_timer;
};

struct sway_output *output_create(struct wlr_output *wlr_output);

void output_destroy(struct sway_output *output);

void output_begin_destroy(struct sway_output *output);

struct sway_output *output_from_wlr_output(struct wlr_output *output);

struct sway_output *output_get_in_direction(struct sway_output *reference,
        enum wlr_direction direction);

// this ONLY includes the enabled outputs
struct sway_output *output_by_name_or_id(const char *name_or_id);

// this includes all the outputs, including disabled ones
struct sway_output *all_output_by_name_or_id(const char *name_or_id);

void output_enable(struct sway_output *output);

void output_disable(struct sway_output *output);

bool output_has_opaque_overlay_layer_surface(struct sway_output *output);

void output_render(struct sway_output *output, struct timespec *when,
    pixman_region32_t *damage);

void output_get_box(struct sway_output *output, struct wlr_box *box);

void output_get_render_box(struct sway_output *output, struct wlr_box *box);

// _box.x and .y are expected to be layout-local
// _box.width and .height are expected to be output-buffer-local
void render_rect(struct sway_output *output,
        pixman_region32_t *output_damage, const struct wlr_box *_box,
        float color[static 4]);

void premultiply_alpha(float color[4], float opacity);

void scale_box(struct wlr_box *box, float scale);

void handle_output_layout_change(struct wl_listener *listener, void *data);

void handle_output_power_manager_set_mode(struct wl_listener *listener,
    void *data);

struct wls_window *output_add_window(struct sway_output *output,
        struct wls_window *win);

bool output_has_windows(struct sway_output *output);

void output_seize_windows_from(struct sway_output *absorber,
    struct sway_output *giver);

void seize_windows_from_noop_output(struct sway_output *output);
#endif
