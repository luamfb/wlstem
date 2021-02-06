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
#include "tree/container.h"
#include "sway/input/tablet.h"
#include "sway/tree/root.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

enum binding_input_type {
    BINDING_KEYCODE,
    BINDING_KEYSYM,
    BINDING_MOUSECODE,
    BINDING_MOUSESYM,
    BINDING_SWITCH
};

enum binding_flags {
    BINDING_RELEASE = 1 << 0,
    BINDING_LOCKED = 1 << 1, // keyboard only
    BINDING_BORDER = 1 << 2, // mouse only; trigger on container border
    BINDING_CONTENTS = 1 << 3, // mouse only; trigger on container contents
    BINDING_TITLEBAR = 1 << 4, // mouse only; trigger on container titlebar
    BINDING_CODE = 1 << 5, // keyboard only; convert keysyms into keycodes
    BINDING_RELOAD = 1 << 6, // switch only; (re)trigger binding on reload
    BINDING_INHIBITED = 1 << 7, // keyboard only: ignore shortcut inhibitor
    BINDING_NOREPEAT = 1 << 8, // keyboard only; do not trigger when repeating a held key
};

typedef bool(*binding_callback_type)(void);

/**
 * A key binding and an associated command.
 */
struct sway_binding {
    enum binding_input_type type;
    int order;
    char *input;
    uint32_t flags;
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
 * A mouse binding and an associated command.
 */
struct sway_mouse_binding {
    uint32_t button;
    binding_callback_type callback;
};

/**
 * A laptop switch binding and an associated command.
 */
struct sway_switch_binding {
    enum wlr_switch_type type;
    enum wlr_switch_state state;
    uint32_t flags;
    binding_callback_type callback;
};

/**
 * Focus on window activation.
 */
enum sway_fowa {
    FOWA_SMART,
    FOWA_URGENT,
    FOWA_FOCUS,
    FOWA_NONE,
};

/**
 * A "mode" of keybindings created via the `mode` command.
 */
struct sway_mode {
    char *name;
    list_t *keysym_bindings;
    list_t *keycode_bindings;
    list_t *mouse_bindings;
    list_t *switch_bindings;
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

struct input_config_tool {
    enum wlr_tablet_tool_type type;
    enum sway_tablet_tool_mode mode;
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

enum seat_config_hide_cursor_when_typing {
    HIDE_WHEN_TYPING_DEFAULT, // the default is currently disabled
    HIDE_WHEN_TYPING_ENABLE,
    HIDE_WHEN_TYPING_DISABLE,
};

enum seat_config_allow_constrain {
    CONSTRAIN_DEFAULT, // the default is currently enabled
    CONSTRAIN_ENABLE,
    CONSTRAIN_DISABLE,
};

enum seat_config_shortcuts_inhibit {
    SHORTCUTS_INHIBIT_DEFAULT, // the default is currently enabled
    SHORTCUTS_INHIBIT_ENABLE,
    SHORTCUTS_INHIBIT_DISABLE,
};

enum seat_keyboard_grouping {
    KEYBOARD_GROUP_DEFAULT, // the default is currently smart
    KEYBOARD_GROUP_NONE,
    KEYBOARD_GROUP_SMART, // keymap and repeat info
};

enum sway_input_idle_source {
    IDLE_SOURCE_KEYBOARD = 1 << 0,
    IDLE_SOURCE_POINTER = 1 << 1,
    IDLE_SOURCE_TOUCH = 1 << 2,
    IDLE_SOURCE_TABLET_PAD = 1 << 3,
    IDLE_SOURCE_TABLET_TOOL = 1 << 4,
    IDLE_SOURCE_SWITCH = 1 << 5,
};

/**
 * Options for multiseat and other misc device configurations
 */
struct seat_config {
    char *name;
    int fallback; // -1 means not set
    list_t *attachments; // list of seat_attachment configs
    int hide_cursor_timeout;
    enum seat_config_hide_cursor_when_typing hide_cursor_when_typing;
    enum seat_config_allow_constrain allow_constrain;
    enum seat_config_shortcuts_inhibit shortcuts_inhibit;
    enum seat_keyboard_grouping keyboard_grouping;
    uint32_t idle_inhibit_sources, idle_wake_sources;
    struct {
        char *name;
        int size;
    } xcursor_theme;
};

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

/**
 * Stores configuration for a workspace, regardless of whether the workspace
 * exists.
 */
struct workspace_config {
    char *workspace;
    list_t *outputs;
};

struct border_colors {
    float border[4];
    float background[4];
    float text[4];
    float indicator[4];
    float child_border[4];
};

enum sway_popup_during_fullscreen {
    POPUP_SMART,
    POPUP_IGNORE,
    POPUP_LEAVE,
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

enum focus_wrapping_mode {
    WRAP_NO,
    WRAP_YES,
    WRAP_FORCE,
    WRAP_WORKSPACE,
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
    list_t *modes;
    list_t *output_configs;
    list_t *input_configs;
    list_t *input_type_configs;
    list_t *seat_configs;
    struct sway_mode *current_mode;
    enum sway_container_layout default_orientation;
    enum sway_container_layout default_layout;
    char *font;
    size_t font_height;
    size_t font_baseline;
    bool pango_markup;
    int titlebar_border_thickness;
    int titlebar_h_padding;
    int titlebar_v_padding;
    size_t urgent_timeout;
    enum sway_fowa focus_on_window_activation;
    enum sway_popup_during_fullscreen popup_during_fullscreen;
    enum xwayland_mode xwayland;

    // Flags
    enum focus_follows_mouse_mode focus_follows_mouse;
    enum mouse_warping_mode mouse_warping;
    enum focus_wrapping_mode focus_wrapping;
    bool active;
    enum alignment title_align;

    enum sway_container_border border;
    enum sway_container_border floating_border;
    int border_thickness;
    int floating_border_thickness;

    // border colors
    struct {
        struct border_colors focused;
        struct border_colors focused_inactive;
        struct border_colors unfocused;
        struct border_colors urgent;
        struct border_colors placeholder;
        float background[4];
    } border_colors;

    // floating view
    int32_t floating_minimum_width;
    int32_t floating_minimum_height;

    // The keysym to keycode translation
    struct xkb_state *keysym_translation_state;

    struct sway_seat *current_seat;
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

void apply_output_config_to_outputs(struct output_config *oc);

void reset_outputs(void);

void free_output_config(struct output_config *oc);

int workspace_output_cmp_workspace(const void *a, const void *b);

void free_sway_binding(struct sway_binding *sb);

void free_switch_binding(struct sway_switch_binding *binding);

void seat_execute_command(struct sway_seat *seat, struct sway_binding *binding);

void free_workspace_config(struct workspace_config *wsc);

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
