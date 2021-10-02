#define _POSIX_C_SOURCE 200809L
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <string.h>
#include <strings.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <wlr/types/wlr_cursor.h>
#include "sway/commands.h"
#include "sway/sway_config.h"
#include "sway/cursor.h"
#include "sway/keyboard.h"
#include "list.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

int binding_order = 0;

void free_sway_binding(struct sway_binding *binding) {
    if (!binding) {
        return;
    }

    list_free_items_and_destroy(binding->keys);
    list_free_items_and_destroy(binding->syms);
    binding->callback = NULL;
    free(binding->input);
    free(binding);
}

/**
 * Returns true if the bindings have the same key and modifier combinations.
 * Note that keyboard layout is not considered, so the bindings might actually
 * not be equivalent on some layouts.
 */
static bool binding_key_compare(struct sway_binding *binding_a,
        struct sway_binding *binding_b) {
    if (strcmp(binding_a->input, binding_b->input) != 0) {
        return false;
    }

    if (binding_a->type != binding_b->type) {
        return false;
    }

    if (binding_a->group != binding_b->group) {
        return false;
    }

    if (binding_a->modifiers ^ binding_b->modifiers) {
        return false;
    }

    if (binding_a->keys->length != binding_b->keys->length) {
        return false;
    }

    // Keys are sorted
    int keys_len = binding_a->keys->length;
    for (int i = 0; i < keys_len; ++i) {
        uint32_t key_a = *(uint32_t *)binding_a->keys->items[i];
        uint32_t key_b = *(uint32_t *)binding_b->keys->items[i];
        if (key_a != key_b) {
            return false;
        }
    }

    return true;
}

static int key_qsort_cmp(const void *keyp_a, const void *keyp_b) {
    uint32_t key_a = **(uint32_t **)keyp_a;
    uint32_t key_b = **(uint32_t **)keyp_b;
    return (key_a < key_b) ? -1 : ((key_a > key_b) ? 1 : 0);
}

/**
 * Insert or update the binding.
 * Return the binding which has been replaced or NULL.
 */
static struct sway_binding *binding_upsert(struct sway_binding *binding,
        list_t *mode_bindings) {
    for (int i = 0; i < mode_bindings->length; ++i) {
        struct sway_binding *config_binding = mode_bindings->items[i];
        if (binding_key_compare(binding, config_binding)) {
            mode_bindings->items[i] = binding;
            return config_binding;
        }
    }

    list_add(mode_bindings, binding);
    return NULL;
}

static bool binding_add(struct sway_binding *binding,
        list_t *mode_bindings, const char *bindtype,
        bool warn) {
    struct sway_binding *config_binding = binding_upsert(binding, mode_bindings);

    if (config_binding) {
        sway_log(SWAY_INFO, "Overwriting binding for device '%s'",
            binding->input);
        free_sway_binding(config_binding);
    } else {
        sway_log(SWAY_DEBUG, "%s - Bound for device '%s'",
                bindtype, binding->input);
    }
    return true;
}

static bool binding_remove(struct sway_binding *binding,
        list_t *mode_bindings, const char *bindtype) {
    for (int i = 0; i < mode_bindings->length; ++i) {
        struct sway_binding *config_binding = mode_bindings->items[i];
        if (binding_key_compare(binding, config_binding)) {
            sway_log(SWAY_DEBUG, "%s - Unbound from device '%s'",
                    bindtype, binding->input);
            free_sway_binding(config_binding);
            free_sway_binding(binding);
            list_del(mode_bindings, i);
            return true;
        }
    }
    free_sway_binding(binding);
    sway_log(SWAY_ERROR,
        "Could not find binding for the given flags");
    return false;
}

static bool cmd_bindsym_or_bindcode(
        uint32_t modifiers,
        xkb_keysym_t key,
        binding_callback_type callback,
        bool unbind) {
    const char *bindtype;
    if (unbind) {
        bindtype = "unbindsym";
    } else {
        bindtype = "bindsym";
    }

    struct sway_binding *binding = calloc(1, sizeof(struct sway_binding));
    if (!binding) {
        sway_log(SWAY_ERROR, "Unable to allocate binding");
        return false;
    }
    binding->input = strdup("*");
    binding->keys = create_list();
    binding->group = XKB_LAYOUT_INVALID;
    binding->modifiers = modifiers;
    binding->type = BINDING_KEYSYM;

    bool warn = true;

    uint32_t *_key = calloc(1, sizeof(uint32_t));
    if (!_key) {
        free_sway_binding(binding);
        sway_log(SWAY_ERROR, "Unable to allocate binding key");
        return false;
    }
    *_key = key;
    list_add(binding->keys, _key);

    // sort ascending
    list_qsort(binding->keys, key_qsort_cmp);

    // translate keysyms into keycodes
    if (!translate_binding(binding)) {
        sway_log(SWAY_INFO,
                "Unable to translate bindsym into bindcode");
    }

    list_t *mode_bindings;
    if (binding->type == BINDING_KEYCODE) {
        mode_bindings = config->current_mode->keycode_bindings;
    } else if (binding->type == BINDING_KEYSYM) {
        mode_bindings = config->current_mode->keysym_bindings;
    }

    if (unbind) {
        return binding_remove(binding, mode_bindings, bindtype);
    }

    binding->callback = callback;
    binding->order = binding_order++;
    return binding_add(binding, mode_bindings, bindtype, warn);
}

bool cmd_bindsym(
        uint32_t modifiers,
        xkb_keysym_t key,
        binding_callback_type callback) {
    return cmd_bindsym_or_bindcode(modifiers, key, callback, false);
}

bool cmd_unbindsym(
        uint32_t modifiers,
        xkb_keysym_t key,
        binding_callback_type callback) {
    return cmd_bindsym_or_bindcode(modifiers, key, callback, true);
}

/**
 * Execute the command associated to a binding
 */
void seat_execute_command(struct sway_seat *seat, struct sway_binding *binding) {
    sway_log(SWAY_DEBUG, "running command for binding");
    if (!binding->callback) {
        sway_log(SWAY_DEBUG,
            "attempted to call NULL binding callback; misconfigured binding?");
    }
    bool success = binding->callback();
    if (!success) {
        sway_log(SWAY_INFO, "command failed for binding!");
    }
}

/**
 * The last found keycode associated with the keysym
 * and the total count of matches.
 */
struct keycode_matches {
    xkb_keysym_t keysym;
    xkb_keycode_t keycode;
    int count;
};

/**
 * Iterate through keycodes in the keymap to find ones matching
 * the specified keysym.
 */
static void find_keycode(struct xkb_keymap *keymap,
        xkb_keycode_t keycode, void *data) {
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(
            config->keysym_translation_state, keycode);

    if (keysym == XKB_KEY_NoSymbol) {
        return;
    }

    struct keycode_matches *matches = data;
    if (matches->keysym == keysym) {
        matches->keycode = keycode;
        matches->count++;
    }
}

/**
 * Return the keycode for the specified keysym.
 */
static struct keycode_matches get_keycode_for_keysym(xkb_keysym_t keysym) {
    struct keycode_matches matches = {
        .keysym = keysym,
        .keycode = XKB_KEYCODE_INVALID,
        .count = 0,
    };

    xkb_keymap_key_for_each(
            xkb_state_get_keymap(config->keysym_translation_state),
            find_keycode, &matches);
    return matches;
}

bool translate_binding(struct sway_binding *binding) {
    switch (binding->type) {
    // a bindsym to translate
    case BINDING_KEYSYM:
        binding->syms = binding->keys;
        binding->keys = create_list();
        break;
    // a bindsym to re-translate
    case BINDING_KEYCODE:
        list_free_items_and_destroy(binding->keys);
        binding->keys = create_list();
        break;
    default:
        return true;
    }

    for (int i = 0; i < binding->syms->length; ++i) {
        xkb_keysym_t *keysym = binding->syms->items[i];
        struct keycode_matches matches = get_keycode_for_keysym(*keysym);

        if (matches.count != 1) {
            sway_log(SWAY_INFO, "Unable to convert keysym %" PRIu32 " into"
                    " a single keycode (found %d matches)",
                    *keysym, matches.count);
            goto error;
        }

        xkb_keycode_t *keycode = malloc(sizeof(xkb_keycode_t));
        if (!keycode) {
            sway_log(SWAY_ERROR, "Unable to allocate memory for a keycode");
            goto error;
        }

        *keycode = matches.keycode;
        list_add(binding->keys, keycode);
    }

    list_qsort(binding->keys, key_qsort_cmp);
    binding->type = BINDING_KEYCODE;
    return true;

error:
    list_free_items_and_destroy(binding->keys);
    binding->type = BINDING_KEYSYM;
    binding->keys = binding->syms;
    binding->syms = NULL;
    return false;
}

void binding_add_translated(struct sway_binding *binding,
        list_t *mode_bindings) {
    struct sway_binding *config_binding =
        binding_upsert(binding, mode_bindings);

    if (config_binding) {
        sway_log(SWAY_INFO, "Overwriting binding for device '%s'",
            binding->input);
        free_sway_binding(config_binding);
    }
}
