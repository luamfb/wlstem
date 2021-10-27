#ifndef _SWAY_CONFIG_H
#define _SWAY_CONFIG_H
#include <libinput.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <wlr/interfaces/wlr_switch.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <xkbcommon/xkbcommon.h>
#include "../include/config.h"
#include "list.h"
#include "container.h"
#include "output_config.h"
#include "tablet.h"
#include "output_manager.h"
#include "util.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

enum binding_input_type {
    BINDING_KEYCODE,
    BINDING_KEYSYM,
};

typedef bool(*binding_callback_type)(void);

/**
 * A key binding and an associated command.
 */
struct sway_binding {
    enum binding_input_type type;
    int order;
    char *input;
    list_t *keys; // sorted in ascending order
    list_t *syms; // sorted in ascending order; NULL if BINDING_CODE is not set
    uint32_t modifiers;
    xkb_layout_index_t group;
    binding_callback_type callback;
};

bool wls_try_exec(char *cmd);

bool cmd_bindsym(
        uint32_t modifiers,
        xkb_keysym_t key,
        binding_callback_type callback);

/**
 * A "mode" of keybindings created via the `mode` command.
 */
struct sway_mode {
    char *name;
    list_t *keysym_bindings;
    list_t *keycode_bindings;
    bool pango;
};

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

/**
 * Options for misc device configurations that happen in the seat block
 */
struct seat_attachment_config {
    char *identifier;
    // TODO other things are configured here for some reason
};

/**
 * Options for multiseat and other misc device configurations
 */
struct seat_config {
    char *name;
    int fallback; // -1 means not set
    list_t *attachments; // list of seat_attachment configs
    int hide_cursor_timeout;
    enum bool_option hide_cursor_when_typing;
    enum bool_option allow_constrain;
    enum bool_option shortcuts_inhibit;
    enum bool_option keyboard_smart_grouping;
    uint32_t idle_inhibit_sources, idle_wake_sources;
    struct {
        char *name;
        int size;
    } xcursor_theme;
};

struct border_colors {
    float border[4];
    float background[4];
    float text[4];
    float indicator[4];
    float child_border[4];
};

enum command_context {
    CONTEXT_CONFIG = 1 << 0,
    CONTEXT_BINDING = 1 << 1,
    CONTEXT_ALL = 0xFFFFFFFF,
};

enum focus_follows_mouse_mode {
    FOLLOWS_NO,
    FOLLOWS_YES,
    FOLLOWS_ALWAYS,
};

enum mouse_warping_mode {
    WARP_NO,
    WARP_OUTPUT,
    WARP_CONTAINER,
};

enum alignment {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT,
};

enum xwayland_mode {
    XWAYLAND_MODE_DISABLED,
    XWAYLAND_MODE_LAZY,
    XWAYLAND_MODE_IMMEDIATE,
};

/**
 * The configuration struct. The result of loading a config file.
 */
struct sway_config {
    list_t *input_configs;
    list_t *input_type_configs;
    list_t *seat_configs;
    struct sway_mode *current_mode;
    char *font;
    size_t font_height;
    size_t font_baseline;
    bool pango_markup;
    int titlebar_border_thickness;
    int titlebar_h_padding;
    int titlebar_v_padding;
    size_t urgent_timeout;
    enum xwayland_mode xwayland;

    // Flags
    enum focus_follows_mouse_mode focus_follows_mouse;
    enum mouse_warping_mode mouse_warping;
    bool active;
    enum alignment title_align;

    int border_thickness;

    // border colors
    struct {
        struct border_colors focused;
        struct border_colors unfocused;
        struct border_colors urgent;
        struct border_colors placeholder;
        float background[4];
    } border_colors;

    // The keysym to keycode translation
    struct xkb_state *keysym_translation_state;
};

/**
 * Loads the main config from the given path.
 */
bool load_main_config(void);

/**
 * Free config struct
 */
void free_config(struct sway_config *config);

int input_identifier_cmp(const void *item, const void *data);

struct input_config *new_input_config(const char* identifier);

void merge_input_config(struct input_config *dst, struct input_config *src);

struct input_config *store_input_config(struct input_config *ic, char **error);

void input_config_fill_rule_names(struct input_config *ic,
        struct xkb_rule_names *rules);

void free_input_config(struct input_config *ic);

int seat_name_cmp(const void *item, const void *data);

struct seat_config *new_seat_config(const char* name);

void merge_seat_config(struct seat_config *dst, struct seat_config *src);

struct seat_config *copy_seat_config(struct seat_config *seat);

void free_seat_config(struct seat_config *ic);

struct seat_attachment_config *seat_attachment_config_new(void);

struct seat_attachment_config *seat_config_get_attachment(
        struct seat_config *seat_config, char *identifier);

struct seat_config *store_seat_config(struct seat_config *seat);

void free_sway_binding(struct sway_binding *sb);

void seat_execute_command(struct sway_seat *seat, struct sway_binding *binding);

/**
 * Updates the value of config->font_height based on the max title height
 * reported by each container. If recalculate is true, the containers will
 * recalculate their heights before reporting.
 *
 * If the height has changed, all containers will be rearranged to take on the
 * new size.
 */
void config_update_font_height(bool recalculate);

/**
 * Convert bindsym into bindcode using the first configured layout.
 * Return false in case the conversion is unsuccessful.
 */
bool translate_binding(struct sway_binding *binding);

void translate_keysyms(struct input_config *input_config);

void binding_add_translated(struct sway_binding *binding, list_t *bindings);

/* Global config singleton. */
extern struct sway_config *config;

#endif
