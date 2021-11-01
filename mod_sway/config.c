#define _XOPEN_SOURCE 700 // for realpath
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <wordexp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <libinput.h>
#include <limits.h>
#include <dirent.h>
#include <strings.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "input_manager.h"
#include "foreach.h"
#include "seat.h"
#include "sway_switch.h"
#include "sway_commands.h"
#include "sway_config.h"
#include "transaction.h"
#include "server_arrange.h"
#include "server_wm.h"
#include "output_manager.h"
#include "cairo.h"
#include "pango.h"
#include "stringop.h"
#include "list.h"
#include "log.h"
#include "util.h"
#include "window_title.h"

struct sway_config *config = NULL;

static struct xkb_state *keysym_translation_state_create(
        struct xkb_rule_names rules) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_names(
        context,
        &rules,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    xkb_context_unref(context);
    return xkb_state_new(xkb_keymap);
}

static void keysym_translation_state_destroy(
        struct xkb_state *state) {
    xkb_keymap_unref(xkb_state_get_keymap(state));
    xkb_state_unref(state);
}

void free_config(struct sway_config *config) {
    if (!config) {
        return;
    }

    if (config->input_configs) {
        for (int i = 0; i < config->input_configs->length; i++) {
            free_input_config(config->input_configs->items[i]);
        }
        list_free(config->input_configs);
    }
    if (config->input_type_configs) {
        for (int i = 0; i < config->input_type_configs->length; i++) {
            free_input_config(config->input_type_configs->items[i]);
        }
        list_free(config->input_type_configs);
    }
    if (config->seat_configs) {
        for (int i = 0; i < config->seat_configs->length; i++) {
            free_seat_config(config->seat_configs->items[i]);
        }
        list_free(config->seat_configs);
    }
    free(config->font);
    keysym_translation_state_destroy(config->keysym_translation_state);
    free(config);
}

void sway_terminate(int exit_code);

bool terminate_ok(void) {
    sway_log(SWAY_DEBUG, "Termination keybinding pressed.");
    sway_terminate(0);
    return true;
}

bool spawn_terminal(void) {
    return wls_try_exec("weston-terminal");
}

static void insert_default_keybindings() {
    cmd_bindsym(WLR_MODIFIER_ALT, XKB_KEY_Return, spawn_terminal);
    cmd_bindsym(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
        XKB_KEY_BackSpace,
        terminate_ok);
}

static void config_defaults(struct sway_config *config) {
    if (!(config->seat_configs = create_list())) goto cleanup;

    if (!(config->input_type_configs = create_list())) goto cleanup;
    if (!(config->input_configs = create_list())) goto cleanup;

    if (!(config->current_mode = malloc(sizeof(struct sway_mode))))
        goto cleanup;
    if (!(config->current_mode->name = malloc(sizeof("default")))) goto cleanup;
    strcpy(config->current_mode->name, "default");
    if (!(config->current_mode->keysym_bindings = create_list())) goto cleanup;
    if (!(config->current_mode->keycode_bindings = create_list())) goto cleanup;

    if (!(config->font = strdup("monospace 10"))) goto cleanup;
    config->font_height = 17; // height of monospace 10
    config->urgent_timeout = 500;
    config->xwayland = XWAYLAND_MODE_LAZY;

    config->titlebar_border_thickness = 1;
    config->titlebar_h_padding = 5;
    config->titlebar_v_padding = 4;

    // Flags
    config->focus_follows_mouse = FOLLOWS_YES;
    config->mouse_warping = WARP_OUTPUT;
    config->active = false;
    config->title_align = ALIGN_LEFT;

    // borders
    config->border_thickness = 2;

    // border colors
    color_to_rgba(config->border_colors.focused.border, 0x4C7899FF);
    color_to_rgba(config->border_colors.focused.background, 0x285577FF);
    color_to_rgba(config->border_colors.focused.text, 0xFFFFFFFF);
    color_to_rgba(config->border_colors.focused.indicator, 0x2E9EF4FF);
    color_to_rgba(config->border_colors.focused.child_border, 0x285577FF);

    color_to_rgba(config->border_colors.unfocused.border, 0x333333FF);
    color_to_rgba(config->border_colors.unfocused.background, 0x222222FF);
    color_to_rgba(config->border_colors.unfocused.text, 0x888888FF);
    color_to_rgba(config->border_colors.unfocused.indicator, 0x292D2EFF);
    color_to_rgba(config->border_colors.unfocused.child_border, 0x222222FF);

    color_to_rgba(config->border_colors.urgent.border, 0x2F343AFF);
    color_to_rgba(config->border_colors.urgent.background, 0x900000FF);
    color_to_rgba(config->border_colors.urgent.text, 0xFFFFFFFF);
    color_to_rgba(config->border_colors.urgent.indicator, 0x900000FF);
    color_to_rgba(config->border_colors.urgent.child_border, 0x900000FF);

    color_to_rgba(config->border_colors.placeholder.border, 0x000000FF);
    color_to_rgba(config->border_colors.placeholder.background, 0x0C0C0CFF);
    color_to_rgba(config->border_colors.placeholder.text, 0xFFFFFFFF);
    color_to_rgba(config->border_colors.placeholder.indicator, 0x000000FF);
    color_to_rgba(config->border_colors.placeholder.child_border, 0x0C0C0CFF);

    color_to_rgba(config->border_colors.background, 0xFFFFFFFF);

    // The keysym to keycode translation
    struct xkb_rule_names rules = {0};
    config->keysym_translation_state =
        keysym_translation_state_create(rules);

    insert_default_keybindings();
    return;
cleanup:
    sway_abort("Unable to allocate config structures");
}

bool load_main_config(void) {
    config = calloc(1, sizeof(struct sway_config));
    if (!config) {
        sway_abort("Unable to allocate config");
    }

    config_defaults(config);
    return true;
}

static void find_font_height_iterator(struct sway_container *con, void *data) {
    size_t amount_below_baseline = con->title_height - con->title_baseline;
    size_t extended_height = config->font_baseline + amount_below_baseline;
    if (extended_height > config->font_height) {
        config->font_height = extended_height;
    }
}

static void find_baseline_iterator(struct sway_container *con, void *data) {
    bool *recalculate = data;
    if (*recalculate) {
        container_calculate_title_height(con);
    }
    if (con->title_baseline > config->font_baseline) {
        config->font_baseline = con->title_baseline;
    }
}

void config_update_font_height(bool recalculate) {
    size_t prev_max_height = config->font_height;
    config->font_height = 0;
    config->font_baseline = 0;

    wls_output_layout_for_each_container(find_baseline_iterator, &recalculate);
    wls_output_layout_for_each_container(find_font_height_iterator, NULL);

    if (config->font_height != prev_max_height) {
        arrange_root();
    }
}

static void translate_binding_list(list_t *bindings, list_t *bindsyms,
        list_t *bindcodes) {
    for (int i = 0; i < bindings->length; ++i) {
        struct sway_binding *binding = bindings->items[i];
        translate_binding(binding);

        switch (binding->type) {
        case BINDING_KEYSYM:
            binding_add_translated(binding, bindsyms);
            break;
        case BINDING_KEYCODE:
            binding_add_translated(binding, bindcodes);
            break;
        default:
            sway_assert(false, "unexpected translated binding type: %d",
                    binding->type);
            break;
        }

    }
}

void translate_keysyms(struct input_config *input_config) {
    keysym_translation_state_destroy(config->keysym_translation_state);

    struct xkb_rule_names rules = {0};
    input_config_fill_rule_names(input_config, &rules);
    config->keysym_translation_state =
        keysym_translation_state_create(rules);

    struct sway_mode *mode = config->current_mode;

    list_t *bindsyms = create_list();
    list_t *bindcodes = create_list();

    translate_binding_list(mode->keysym_bindings, bindsyms, bindcodes);
    translate_binding_list(mode->keycode_bindings, bindsyms, bindcodes);

    list_free(mode->keysym_bindings);
    list_free(mode->keycode_bindings);

    mode->keysym_bindings = bindsyms;
    mode->keycode_bindings = bindcodes;

    sway_log(SWAY_DEBUG, "Translated keysyms using config for device '%s'",
            input_config->identifier);
}
