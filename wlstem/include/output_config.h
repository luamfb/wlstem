#ifndef _SWAY_OUTPUT_CONFIG_H
#define _SWAY_OUTPUT_CONFIG_H

#include <wayland-server.h>

struct sway_output;

enum config_dpms {
    DPMS_IGNORE,
    DPMS_ON,
    DPMS_OFF,
};

enum scale_filter_mode {
    SCALE_FILTER_DEFAULT, // the default is currently smart
    SCALE_FILTER_LINEAR,
    SCALE_FILTER_NEAREST,
    SCALE_FILTER_SMART,
};

/**
 * Size and position configuration for a particular output.
 *
 * This is set via the `output` command.
 */
struct output_config {
    char *name;
    int enabled;
    int width, height;
    float refresh_rate;
    int custom_mode;
    int x, y;
    float scale;
    enum scale_filter_mode scale_filter;
    int32_t transform;
    enum wl_output_subpixel subpixel;
    int max_render_time; // In milliseconds
    int adaptive_sync;

    char *background;
    char *background_option;
    char *background_fallback;
    enum config_dpms dpms_state;
};

int output_name_cmp(const void *item, const void *data);

void output_get_identifier(char *identifier, size_t len,
    struct sway_output *output);

const char *sway_output_scale_filter_to_string(enum scale_filter_mode scale_filter);

struct output_config *new_output_config(const char *name);

void merge_output_config(struct output_config *dst, struct output_config *src);

bool apply_output_config(struct output_config *oc, struct sway_output *output);

bool test_output_config(struct output_config *oc, struct sway_output *output);

struct output_config *store_output_config(struct output_config *oc);

struct output_config *find_output_config(struct sway_output *output);

void free_output_config(struct output_config *oc);

#endif
