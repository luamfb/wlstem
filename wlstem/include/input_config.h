#ifndef WLSTEM_INPUT_CONFIG_H_
#define WLSTEM_INPUT_CONFIG_H_

#include <stdbool.h>
#include <wlr/types/wlr_box.h>
#include "list.h"

struct input_config_mapped_from_region {
    double x1, y1;
    double x2, y2;
    bool mm;
};

struct calibration_matrix {
    bool configured;
    float matrix[6];
};

enum input_config_mapped_to {
    MAPPED_TO_DEFAULT,
    MAPPED_TO_OUTPUT,
    MAPPED_TO_REGION,
};

/**
 * options for input devices
 */
struct input_config {
    char *identifier;
    const char *input_type;

    int accel_profile;
    struct calibration_matrix calibration_matrix;
    int click_method;
    int drag;
    int drag_lock;
    int dwt;
    int left_handed;
    int middle_emulation;
    int natural_scroll;
    float pointer_accel;
    float scroll_factor;
    int repeat_delay;
    int repeat_rate;
    int scroll_button;
    int scroll_method;
    int send_events;
    int tap;
    int tap_button_map;

    char *xkb_layout;
    char *xkb_model;
    char *xkb_options;
    char *xkb_rules;
    char *xkb_variant;
    char *xkb_file;

    bool xkb_file_is_set;

    int xkb_numlock;
    int xkb_capslock;

    struct input_config_mapped_from_region *mapped_from_region;

    enum input_config_mapped_to mapped_to;
    char *mapped_to_output;
    struct wlr_box *mapped_to_region;

    list_t *tools;

    bool capturable;
    struct wlr_box region;
};

#endif /* WLSTEM_INPUT_CONFIG_H_ */
