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
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/input/switch.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/criteria.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "cairo.h"
#include "pango.h"
#include "stringop.h"
#include "list.h"
#include "log.h"
#include "util.h"

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

static void free_mode(struct sway_mode *mode) {
    if (!mode) {
        return;
    }
    free(mode->name);
    if (mode->keysym_bindings) {
        for (int i = 0; i < mode->keysym_bindings->length; i++) {
            free_sway_binding(mode->keysym_bindings->items[i]);
        }
        list_free(mode->keysym_bindings);
    }
    if (mode->keycode_bindings) {
        for (int i = 0; i < mode->keycode_bindings->length; i++) {
            free_sway_binding(mode->keycode_bindings->items[i]);
        }
        list_free(mode->keycode_bindings);
    }
    if (mode->mouse_bindings) {
        for (int i = 0; i < mode->mouse_bindings->length; i++) {
            free_sway_binding(mode->mouse_bindings->items[i]);
        }
        list_free(mode->mouse_bindings);
    }
    if (mode->switch_bindings) {
        for (int i = 0; i < mode->switch_bindings->length; i++) {
            free_switch_binding(mode->switch_bindings->items[i]);
        }
        list_free(mode->switch_bindings);
    }
    free(mode);
}

void free_config(struct sway_config *config) {
    if (!config) {
        return;
    }

    memset(&config->handler_context, 0, sizeof(config->handler_context));

    // TODO: handle all currently unhandled lists as we add implementations
    if (config->symbols) {
        for (int i = 0; i < config->symbols->length; ++i) {
            free_sway_variable(config->symbols->items[i]);
        }
        list_free(config->symbols);
    }
    if (config->modes) {
        for (int i = 0; i < config->modes->length; ++i) {
            free_mode(config->modes->items[i]);
        }
        list_free(config->modes);
    }
    list_free(config->cmd_queue);
    if (config->workspace_configs) {
        for (int i = 0; i < config->workspace_configs->length; i++) {
            free_workspace_config(config->workspace_configs->items[i]);
        }
        list_free(config->workspace_configs);
    }
    if (config->output_configs) {
        for (int i = 0; i < config->output_configs->length; i++) {
            free_output_config(config->output_configs->items[i]);
        }
        list_free(config->output_configs);
    }
    if (config->swaybg_client != NULL) {
        wl_client_destroy(config->swaybg_client);
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
    if (config->criteria) {
        for (int i = 0; i < config->criteria->length; ++i) {
            criteria_destroy(config->criteria->items[i]);
        }
        list_free(config->criteria);
    }
    list_free(config->no_focus);
    free(config->floating_scroll_up_cmd);
    free(config->floating_scroll_down_cmd);
    free(config->floating_scroll_left_cmd);
    free(config->floating_scroll_right_cmd);
    free(config->font);
    free(config->swaybg_command);
    free((char *)config->current_config);
    keysym_translation_state_destroy(config->keysym_translation_state);
    free(config);
}

static void config_defaults(struct sway_config *config) {
    if (!(config->symbols = create_list())) goto cleanup;
    if (!(config->modes = create_list())) goto cleanup;
    if (!(config->workspace_configs = create_list())) goto cleanup;
    if (!(config->criteria = create_list())) goto cleanup;
    if (!(config->no_focus = create_list())) goto cleanup;
    if (!(config->seat_configs = create_list())) goto cleanup;
    if (!(config->output_configs = create_list())) goto cleanup;

    if (!(config->input_type_configs = create_list())) goto cleanup;
    if (!(config->input_configs = create_list())) goto cleanup;

    if (!(config->cmd_queue = create_list())) goto cleanup;

    if (!(config->current_mode = malloc(sizeof(struct sway_mode))))
        goto cleanup;
    if (!(config->current_mode->name = malloc(sizeof("default")))) goto cleanup;
    strcpy(config->current_mode->name, "default");
    if (!(config->current_mode->keysym_bindings = create_list())) goto cleanup;
    if (!(config->current_mode->keycode_bindings = create_list())) goto cleanup;
    if (!(config->current_mode->mouse_bindings = create_list())) goto cleanup;
    if (!(config->current_mode->switch_bindings = create_list())) goto cleanup;
    list_add(config->modes, config->current_mode);

    config->floating_mod = 0;
    config->floating_mod_inverse = false;
    config->dragging_key = BTN_LEFT;
    config->resizing_key = BTN_RIGHT;

    if (!(config->floating_scroll_up_cmd = strdup(""))) goto cleanup;
    if (!(config->floating_scroll_down_cmd = strdup(""))) goto cleanup;
    if (!(config->floating_scroll_left_cmd = strdup(""))) goto cleanup;
    if (!(config->floating_scroll_right_cmd = strdup(""))) goto cleanup;
    config->default_layout = L_NONE;
    config->default_orientation = L_NONE;
    if (!(config->font = strdup("monospace 10"))) goto cleanup;
    config->font_height = 17; // height of monospace 10
    config->urgent_timeout = 500;
    config->focus_on_window_activation = FOWA_URGENT;
    config->popup_during_fullscreen = POPUP_SMART;
    config->xwayland = XWAYLAND_MODE_LAZY;

    config->titlebar_border_thickness = 1;
    config->titlebar_h_padding = 5;
    config->titlebar_v_padding = 4;

    // floating view
    config->floating_maximum_width = 0;
    config->floating_maximum_height = 0;
    config->floating_minimum_width = 75;
    config->floating_minimum_height = 50;

    // Flags
    config->focus_follows_mouse = FOLLOWS_YES;
    config->mouse_warping = WARP_OUTPUT;
    config->focus_wrapping = WRAP_YES;
    config->active = false;
    config->failed = false;
    config->auto_back_and_forth = false;
    config->show_marks = true;
    config->title_align = ALIGN_LEFT;
    config->tiling_drag = true;
    config->tiling_drag_threshold = 9;

    config->smart_gaps = false;
    config->gaps_inner = 0;
    config->gaps_outer.top = 0;
    config->gaps_outer.right = 0;
    config->gaps_outer.bottom = 0;
    config->gaps_outer.left = 0;

    if (!(config->swaybg_command = strdup("swaybg"))) goto cleanup;

    config->current_config = NULL;

    // borders
    config->border = B_NORMAL;
    config->floating_border = B_NORMAL;
    config->border_thickness = 2;
    config->floating_border_thickness = 2;
    config->hide_edge_borders = E_NONE;
    config->hide_edge_borders_smart = ESMART_OFF;
    config->hide_lone_tab = false;

    // border colors
    color_to_rgba(config->border_colors.focused.border, 0x4C7899FF);
    color_to_rgba(config->border_colors.focused.background, 0x285577FF);
    color_to_rgba(config->border_colors.focused.text, 0xFFFFFFFF);
    color_to_rgba(config->border_colors.focused.indicator, 0x2E9EF4FF);
    color_to_rgba(config->border_colors.focused.child_border, 0x285577FF);

    color_to_rgba(config->border_colors.focused_inactive.border, 0x333333FF);
    color_to_rgba(config->border_colors.focused_inactive.background, 0x5F676AFF);
    color_to_rgba(config->border_colors.focused_inactive.text, 0xFFFFFFFF);
    color_to_rgba(config->border_colors.focused_inactive.indicator, 0x484E50FF);
    color_to_rgba(config->border_colors.focused_inactive.child_border, 0x5F676AFF);

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

void load_include_configs(const char *path, struct sway_config *config) {
}

void run_deferred_commands(void) {
    if (!config->cmd_queue->length) {
        return;
    }
    sway_log(SWAY_DEBUG, "Running deferred commands");
    while (config->cmd_queue->length) {
        char *line = config->cmd_queue->items[0];
        list_t *res_list = execute_command(line, NULL, NULL);
        for (int i = 0; i < res_list->length; ++i) {
            struct cmd_results *res = res_list->items[i];
            if (res->status != CMD_SUCCESS) {
                sway_log(SWAY_ERROR, "Error on line '%s': %s",
                        line, res->error);
            }
            free_cmd_results(res);
        }
        list_del(config->cmd_queue, 0);
        list_free(res_list);
        free(line);
    }
}

void run_deferred_bindings(void) {
    struct sway_seat *seat;
    wl_list_for_each(seat, &(server.input->seats), link) {
        if (!seat->deferred_bindings->length) {
            continue;
        }
        sway_log(SWAY_DEBUG, "Running deferred bindings for seat %s",
                seat->wlr_seat->name);
        while (seat->deferred_bindings->length) {
            struct sway_binding *binding = seat->deferred_bindings->items[0];
            seat_execute_command(seat, binding);
            list_del(seat->deferred_bindings, 0);
            free_sway_binding(binding);
        }
    }
}

// get line, with backslash continuation
static ssize_t getline_with_cont(char **lineptr, size_t *line_size, FILE *file,
        int *nlines) {
    char *next_line = NULL;
    size_t next_line_size = 0;
    ssize_t nread = getline(lineptr, line_size, file);
    *nlines = nread == -1 ? 0 : 1;
    while (nread >= 2 && strcmp(&(*lineptr)[nread - 2], "\\\n") == 0 && (*lineptr)[0] != '#') {
        ssize_t next_nread = getline(&next_line, &next_line_size, file);
        if (next_nread == -1) {
            break;
        }
        (*nlines)++;

        nread += next_nread - 2;
        if ((ssize_t) *line_size < nread + 1) {
            *line_size = nread + 1;
            char *old_ptr = *lineptr;
            *lineptr = realloc(*lineptr, *line_size);
            if (!*lineptr) {
                free(old_ptr);
                nread = -1;
                break;
            }
        }
        strcpy(&(*lineptr)[nread - next_nread], next_line);
    }
    free(next_line);
    return nread;
}

static int detect_brace(FILE *file) {
    int ret = 0;
    int lines = 0;
    long pos = ftell(file);
    char *line = NULL;
    size_t line_size = 0;
    while ((getline(&line, &line_size, file)) != -1) {
        lines++;
        strip_whitespace(line);
        if (*line) {
            if (strcmp(line, "{") == 0) {
                ret = lines;
            }
            break;
        }
    }
    free(line);
    if (ret == 0) {
        fseek(file, pos, SEEK_SET);
    }
    return ret;
}

static char *expand_line(const char *block, const char *line, bool add_brace) {
    int size = (block ? strlen(block) + 1 : 0) + strlen(line)
        + (add_brace ? 2 : 0) + 1;
    char *expanded = calloc(1, size);
    if (!expanded) {
        sway_log(SWAY_ERROR, "Cannot allocate expanded line buffer");
        return NULL;
    }
    snprintf(expanded, size, "%s%s%s%s", block ? block : "",
            block ? " " : "", line, add_brace ? " {" : "");
    return expanded;
}

bool read_config(FILE *file, struct sway_config *config) {
    bool reading_main_config = false;
    char *this_config = NULL;
    size_t config_size = 0;
    if (config->current_config == NULL) {
        reading_main_config = true;

        int ret_seek = fseek(file, 0, SEEK_END);
        long ret_tell = ftell(file);
        if (ret_seek == -1 || ret_tell == -1) {
            sway_log(SWAY_ERROR, "Unable to get size of config file");
            return false;
        }
        config_size = ret_tell;
        rewind(file);

        config->current_config = this_config = calloc(1, config_size + 1);
        if (this_config == NULL) {
            sway_log(SWAY_ERROR, "Unable to allocate buffer for config contents");
            return false;
        }
    }

    bool success = true;
    int line_number = 0;
    char *line = NULL;
    size_t line_size = 0;
    ssize_t nread;
    list_t *stack = create_list();
    size_t read = 0;
    int nlines = 0;
    while ((nread = getline_with_cont(&line, &line_size, file, &nlines)) != -1) {
        if (reading_main_config) {
            if (read + nread > config_size) {
                sway_log(SWAY_ERROR, "Config file changed during reading");
                success = false;
                break;
            }

            strcpy(&this_config[read], line);
            read += nread;
        }

        if (line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        line_number += nlines;
        sway_log(SWAY_DEBUG, "Read line %d: %s", line_number, line);

        strip_whitespace(line);
        if (!*line || line[0] == '#') {
            continue;
        }
        int brace_detected = 0;
        if (line[strlen(line) - 1] != '{' && line[strlen(line) - 1] != '}') {
            brace_detected = detect_brace(file);
            if (brace_detected > 0) {
                line_number += brace_detected;
                sway_log(SWAY_DEBUG, "Detected open brace on line %d", line_number);
            }
        }
        char *block = stack->length ? stack->items[0] : NULL;
        char *expanded = expand_line(block, line, brace_detected > 0);
        if (!expanded) {
            success = false;
            break;
        }
        config->current_config_line_number = line_number;
        config->current_config_line = line;
        struct cmd_results *res;
        char *new_block = NULL;
        if (block && strcmp(block, "<commands>") == 0) {
            // Special case
            res = config_commands_command(expanded);
        } else {
            res = config_command(expanded, &new_block);
        }
        switch(res->status) {
        case CMD_FAILURE:
        case CMD_INVALID:
            sway_log(SWAY_ERROR, "Error on line %i '%s': %s", line_number,
                line, res->error);
            success = false;
            break;

        case CMD_DEFER:
            sway_log(SWAY_DEBUG, "Deferring command `%s'", line);
            list_add(config->cmd_queue, strdup(expanded));
            break;

        case CMD_BLOCK_COMMANDS:
            sway_log(SWAY_DEBUG, "Entering commands block");
            list_insert(stack, 0, "<commands>");
            break;

        case CMD_BLOCK:
            sway_log(SWAY_DEBUG, "Entering block '%s'", new_block);
            list_insert(stack, 0, strdup(new_block));
            break;

        case CMD_BLOCK_END:
            if (!block) {
                sway_log(SWAY_DEBUG, "Unmatched '}' on line %i", line_number);
                success = false;
                break;
            }

            sway_log(SWAY_DEBUG, "Exiting block '%s'", block);
            list_del(stack, 0);
            free(block);
            memset(&config->handler_context, 0,
                    sizeof(config->handler_context));
        default:;
        }
        free(new_block);
        free(expanded);
        free_cmd_results(res);
    }
    free(line);
    list_free_items_and_destroy(stack);
    config->current_config_line_number = 0;
    config->current_config_line = NULL;

    return success;
}

char *do_var_replacement(char *str) {
    int i;
    char *find = str;
    while ((find = strchr(find, '$'))) {
        // Skip if escaped.
        if (find > str && find[-1] == '\\') {
            if (find == str + 1 || !(find > str + 1 && find[-2] == '\\')) {
                ++find;
                continue;
            }
        }
        // Unescape double $ and move on
        if (find[1] == '$') {
            size_t length = strlen(find + 1);
            memmove(find, find + 1, length);
            find[length] = '\0';
            ++find;
            continue;
        }
        // Find matching variable
        for (i = 0; i < config->symbols->length; ++i) {
            struct sway_variable *var = config->symbols->items[i];
            int vnlen = strlen(var->name);
            if (strncmp(find, var->name, vnlen) == 0) {
                int vvlen = strlen(var->value);
                char *newstr = malloc(strlen(str) - vnlen + vvlen + 1);
                if (!newstr) {
                    sway_log(SWAY_ERROR,
                        "Unable to allocate replacement "
                        "during variable expansion");
                    break;
                }
                char *newptr = newstr;
                int offset = find - str;
                strncpy(newptr, str, offset);
                newptr += offset;
                strncpy(newptr, var->value, vvlen);
                newptr += vvlen;
                strcpy(newptr, find + vnlen);
                free(str);
                str = newstr;
                find = str + offset + vvlen;
                break;
            }
        }
        if (i == config->symbols->length) {
            ++find;
        }
    }
    return str;
}

// the naming is intentional (albeit long): a workspace_output_cmp function
// would compare two structs in full, while this method only compares the
// workspace.
int workspace_output_cmp_workspace(const void *a, const void *b) {
    const struct workspace_config *wsa = a, *wsb = b;
    return lenient_strcmp(wsa->workspace, wsb->workspace);
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

    root_for_each_container(find_baseline_iterator, &recalculate);
    root_for_each_container(find_font_height_iterator, NULL);

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

    for (int i = 0; i < config->modes->length; ++i) {
        struct sway_mode *mode = config->modes->items[i];

        list_t *bindsyms = create_list();
        list_t *bindcodes = create_list();

        translate_binding_list(mode->keysym_bindings, bindsyms, bindcodes);
        translate_binding_list(mode->keycode_bindings, bindsyms, bindcodes);

        list_free(mode->keysym_bindings);
        list_free(mode->keycode_bindings);

        mode->keysym_bindings = bindsyms;
        mode->keycode_bindings = bindcodes;
    }

    sway_log(SWAY_DEBUG, "Translated keysyms using config for device '%s'",
            input_config->identifier);
}
