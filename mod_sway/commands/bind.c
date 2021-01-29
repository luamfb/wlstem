#define _POSIX_C_SOURCE 200809L
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <string.h>
#include <strings.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <wlr/types/wlr_cursor.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/input/cursor.h"
#include "sway/input/keyboard.h"
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

void free_switch_binding(struct sway_switch_binding *binding) {
	if (!binding) {
		return;
	}
	free(binding);
}

/**
 * Returns true if the bindings have the same switch type and state combinations.
 */
static bool binding_switch_compare(struct sway_switch_binding *binding_a,
		struct sway_switch_binding *binding_b) {
	if (binding_a->type != binding_b->type) {
		return false;
	}
	if (binding_a->state != binding_b->state) {
		return false;
	}
	if ((binding_a->flags & BINDING_LOCKED) !=
			(binding_b->flags & BINDING_LOCKED)) {
		return false;
	}
	return true;
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

	uint32_t conflict_generating_flags = BINDING_RELEASE | BINDING_BORDER
			| BINDING_CONTENTS | BINDING_TITLEBAR | BINDING_LOCKED
			| BINDING_INHIBITED;
	if ((binding_a->flags & conflict_generating_flags) !=
			(binding_b->flags & conflict_generating_flags)) {
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
 * From a keycode, bindcode, or bindsym name and the most likely binding type,
 * identify the appropriate numeric value corresponding to the key. Return true
 * and set *key_val if successful, otherwise return false. Change
 * the value of *type if the initial type guess was incorrect and if this
 * was the first identified key.
 */
static bool identify_key(const char* name, bool first_key,
		uint32_t* key_val, enum binding_input_type* type) {
	if (*type == BINDING_MOUSECODE) {
		// check for mouse bindcodes
		char *message = NULL;
		uint32_t button = get_mouse_bindcode(name, &message);
		if (!button) {
			if (message) {
                sway_log(SWAY_ERROR, "%s\n", message);
				free(message);
                return false;
			} else {
				sway_log(SWAY_ERROR, "Unknown button code %s", name);
                return false;
			}
		}
		*key_val = button;
	} else if (*type == BINDING_MOUSESYM) {
		// check for mouse bindsyms (x11 buttons or event names)
		char *message = NULL;
		uint32_t button = get_mouse_bindsym(name, &message);
		if (!button) {
			if (message) {
                sway_log(SWAY_ERROR, "%s\n", message);
				free(message);
				return false;
			} else {
				sway_log(SWAY_ERROR, "Unknown button %s", name);
                return false;
			}
		}
		*key_val = button;
	} else if (*type == BINDING_KEYCODE) {
		// check for keycode. If it is the first key, allow mouse bindcodes
		if (first_key) {
			char *message = NULL;
			uint32_t button = get_mouse_bindcode(name, &message);
			free(message);
			if (button) {
				*type = BINDING_MOUSECODE;
				*key_val = button;
				return true;
			}
		}

		xkb_keycode_t keycode = strtol(name, NULL, 10);
		if (!xkb_keycode_is_legal_ext(keycode)) {
			if (first_key) {
				sway_log(SWAY_ERROR,
						"Invalid keycode or button code '%s'", name);
                return false;
			} else {
				sway_log(SWAY_ERROR, "Invalid keycode '%s'", name);
                return false;
			}
		}
		*key_val = keycode;
	} else {
		// check for keysym. If it is the first key, allow mouse bindsyms
		if (first_key) {
			char *message = NULL;
			uint32_t button = get_mouse_bindsym(name, &message);
			if (message) {
                sway_log(SWAY_ERROR, "%s\n", message);
				free(message);
				return false;
			} else if (button) {
				*type = BINDING_MOUSESYM;
				*key_val = button;
				return true;
			}
		}

		xkb_keysym_t keysym = xkb_keysym_from_name(name,
				XKB_KEYSYM_CASE_INSENSITIVE);
		if (!keysym) {
			if (first_key) {
				sway_log(SWAY_ERROR,
						"Unknown key or button '%s'", name);
                return false;
			} else {
				sway_log(SWAY_ERROR, "Unknown key '%s'", name);
                return false;
			}
		}
		*key_val = keysym;
	}
	return true;
}

static bool switch_binding_add(
		struct sway_switch_binding *binding, const char *bindtype,
		const char *switchcombo, bool warn) {
	list_t *mode_bindings = config->current_mode->switch_bindings;
	// overwrite the binding if it already exists
	bool overwritten = false;
	for (int i = 0; i < mode_bindings->length; ++i) {
		struct sway_switch_binding *config_binding = mode_bindings->items[i];
		if (binding_switch_compare(binding, config_binding)) {
			sway_log(SWAY_INFO, "Overwriting binding '%s'", switchcombo);
			free_switch_binding(config_binding);
			mode_bindings->items[i] = binding;
			overwritten = true;
		}
	}

	if (!overwritten) {
		list_add(mode_bindings, binding);
		sway_log(SWAY_DEBUG, "%s - Bound %s", bindtype, switchcombo);
	}

	return true;
}

static bool switch_binding_remove(
		struct sway_switch_binding *binding, const char *bindtype,
		const char *switchcombo) {
	list_t *mode_bindings = config->current_mode->switch_bindings;
	for (int i = 0; i < mode_bindings->length; ++i) {
		struct sway_switch_binding *config_binding = mode_bindings->items[i];
		if (binding_switch_compare(binding, config_binding)) {
			free_switch_binding(config_binding);
			free_switch_binding(binding);
			list_del(mode_bindings, i);
			sway_log(SWAY_DEBUG, "%s - Unbound %s switch",
					bindtype, switchcombo);
			return true;
		}
	}

	free_switch_binding(binding);
    sway_log(SWAY_ERROR, "Could not find switch binding `%s`", switchcombo);
	return false;
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
		const char *keycombo, bool warn) {
	struct sway_binding *config_binding = binding_upsert(binding, mode_bindings);

	if (config_binding) {
		sway_log(SWAY_INFO, "Overwriting binding '%s' for device '%s'",
            keycombo, binding->input);
		free_sway_binding(config_binding);
	} else {
		sway_log(SWAY_DEBUG, "%s - Bound %s for device '%s'",
				bindtype, keycombo, binding->input);
	}
	return true;
}

static bool binding_remove(struct sway_binding *binding,
		list_t *mode_bindings, const char *bindtype,
		const char *keycombo) {
	for (int i = 0; i < mode_bindings->length; ++i) {
		struct sway_binding *config_binding = mode_bindings->items[i];
		if (binding_key_compare(binding, config_binding)) {
			sway_log(SWAY_DEBUG, "%s - Unbound `%s` from device '%s'",
					bindtype, keycombo, binding->input);
			free_sway_binding(config_binding);
			free_sway_binding(binding);
			list_del(mode_bindings, i);
			return true;
		}
	}
	free_sway_binding(binding);
	sway_log(SWAY_ERROR,
        "Could not find binding `%s` for the given flags",
        keycombo);
    return false;
}

static bool cmd_bindsym_or_bindcode(int argc, char **argv,
        binding_callback_type callback, bool unbind) {
	const char *bindtype;
	int minargs = 1;
	if (unbind) {
		bindtype = "unbindsym";
	} else {
		bindtype = "bindsym";
	}

	if (!checkarg(argc, bindtype, EXPECTED_AT_LEAST, minargs)) {
        sway_log(SWAY_ERROR, "checkarg error!");
		return false;
	}

	struct sway_binding *binding = calloc(1, sizeof(struct sway_binding));
	if (!binding) {
        sway_log(SWAY_ERROR, "Unable to allocate binding");
		return false;
	}
	binding->input = strdup("*");
	binding->keys = create_list();
	binding->group = XKB_LAYOUT_INVALID;
	binding->modifiers = 0;
	binding->flags = 0;
	binding->type = BINDING_KEYSYM;

	bool exclude_titlebar = false;
	bool warn = true;

	if (binding->flags & (BINDING_BORDER | BINDING_CONTENTS | BINDING_TITLEBAR)
			|| exclude_titlebar) {
		binding->type = binding->type == BINDING_KEYCODE ?
			BINDING_MOUSECODE : BINDING_MOUSESYM;
	}

	if (argc < minargs) {
		free_sway_binding(binding);
		sway_log(SWAY_ERROR,
			"Invalid %s command "
			"(expected at least %d non-option arguments, got %d)",
			bindtype, minargs, argc);
        return false;
	}

	list_t *split = split_string(argv[0], "+");
	for (int i = 0; i < split->length; ++i) {
		// Check for group
		if (strncmp(split->items[i], "Group", strlen("Group")) == 0) {
			if (binding->group != XKB_LAYOUT_INVALID) {
				free_sway_binding(binding);
				list_free_items_and_destroy(split);
				sway_log(SWAY_ERROR,
						"Only one group can be specified");
                return false;
			}
			char *end;
			int group = strtol(split->items[i] + strlen("Group"), &end, 10);
			if (group < 1 || group > 4 || end[0] != '\0') {
				free_sway_binding(binding);
				list_free_items_and_destroy(split);
				sway_log(SWAY_ERROR, "Invalid group");
                return false;
			}
			binding->group = group - 1;
			continue;
		} else if (strcmp(split->items[i], "Mode_switch") == 0) {
			// For full i3 compatibility, Mode_switch is an alias for Group2
			if (binding->group != XKB_LAYOUT_INVALID) {
				free_sway_binding(binding);
				list_free_items_and_destroy(split);
				sway_log(SWAY_ERROR,
						"Only one group can be specified");
                return false;
			}
			binding->group = 1;
		}

		// Check for a modifier key
		uint32_t mod;
		if ((mod = get_modifier_mask_by_name(split->items[i])) > 0) {
			binding->modifiers |= mod;
			continue;
		}

		// Identify the key and possibly change binding->type
		uint32_t key_val = 0;
		bool success = identify_key(split->items[i], binding->keys->length == 0,
				     &key_val, &binding->type);
		if (!success) {
			free_sway_binding(binding);
			list_free(split);
			return false;
		}

		uint32_t *key = calloc(1, sizeof(uint32_t));
		if (!key) {
			free_sway_binding(binding);
			list_free_items_and_destroy(split);
			sway_log(SWAY_ERROR, "Unable to allocate binding key");
            return false;
		}
		*key = key_val;
		list_add(binding->keys, key);
	}
	list_free_items_and_destroy(split);

	// refine region of interest for mouse binding once we are certain
	// that this is one
	if (exclude_titlebar) {
		binding->flags &= ~BINDING_TITLEBAR;
	} else if (binding->type == BINDING_MOUSECODE
			|| binding->type == BINDING_MOUSESYM) {
		binding->flags |= BINDING_TITLEBAR;
	}

	// sort ascending
	list_qsort(binding->keys, key_qsort_cmp);

	// translate keysyms into keycodes
	if (!translate_binding(binding)) {
		sway_log(SWAY_INFO,
				"Unable to translate bindsym into bindcode: %s", argv[0]);
	}

	list_t *mode_bindings;
	if (binding->type == BINDING_KEYCODE) {
		mode_bindings = config->current_mode->keycode_bindings;
	} else if (binding->type == BINDING_KEYSYM) {
		mode_bindings = config->current_mode->keysym_bindings;
	} else {
		mode_bindings = config->current_mode->mouse_bindings;
	}

	if (unbind) {
		return binding_remove(binding, mode_bindings, bindtype, argv[0]);
	}

	binding->callback = callback;
	binding->order = binding_order++;
	return binding_add(binding, mode_bindings, bindtype, argv[0], warn);
}

bool cmd_bind_or_unbind_switch(int argc, char **argv,
		binding_callback_type callback, bool unbind) {
	int minargs = 2;
	char *bindtype = "bindswitch";
	if (unbind) {
		minargs--;
		bindtype = "unbindswitch";
	}

	if (!checkarg(argc, bindtype, EXPECTED_AT_LEAST, minargs)) {
		sway_log(SWAY_ERROR, "checkarg error!");
        return false;
	}
	struct sway_switch_binding *binding = calloc(1, sizeof(struct sway_switch_binding));
	if (!binding) {
		sway_log(SWAY_ERROR, "Unable to allocate binding");
        return false;
	}

	bool warn = true;

	if (argc < minargs) {
		free(binding);
		sway_log(SWAY_ERROR,
				"Invalid %s command (expected at least %d "
				"non-option arguments, got %d)", bindtype, minargs, argc);
        return false;
	}

	list_t *split = split_string(argv[0], ":");
	if (split->length != 2) {
		free_switch_binding(binding);
		sway_log(SWAY_ERROR,
				"Invalid %s command (expected binding with the form "
				"<switch>:<state>)", bindtype);
        return false;
	}
	if (strcmp(split->items[0], "tablet") == 0) {
		binding->type = WLR_SWITCH_TYPE_TABLET_MODE;
	} else if (strcmp(split->items[0], "lid") == 0) {
		binding->type = WLR_SWITCH_TYPE_LID;
	} else {
		free_switch_binding(binding);
		sway_log(SWAY_ERROR,
				"Invalid %s command (expected switch binding: "
				"unknown switch)", bindtype);
        return false;
	}
	if (strcmp(split->items[1], "on") == 0) {
		binding->state = WLR_SWITCH_STATE_ON;
	} else if (strcmp(split->items[1], "off") == 0) {
		binding->state = WLR_SWITCH_STATE_OFF;
	} else if (strcmp(split->items[1], "toggle") == 0) {
		binding->state = WLR_SWITCH_STATE_TOGGLE;
	} else {
		free_switch_binding(binding);
		sway_log(SWAY_ERROR,
				"Invalid %s command ", bindtype);
        return false;
	}
	list_free_items_and_destroy(split);

	if (unbind) {
        return switch_binding_remove(binding, bindtype, argv[0]);
	}
	binding->callback = callback;
	return switch_binding_add(binding, bindtype, argv[0], warn);
}

bool cmd_bindsym(int argc, char **argv,
        binding_callback_type callback) {
	return cmd_bindsym_or_bindcode(argc, argv, callback, false);
}

bool cmd_unbindsym(int argc, char **argv,
        binding_callback_type callback) {
	return cmd_bindsym_or_bindcode(argc, argv, callback, true);
}

bool cmd_bindswitch(int argc, char **argv,
        binding_callback_type callback) {
	return cmd_bind_or_unbind_switch(argc, argv, callback, false);
}

bool cmd_unbindswitch(int argc, char **argv,
        binding_callback_type callback) {
	return cmd_bind_or_unbind_switch(argc, argv, callback, true);
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
	if ((binding->flags & BINDING_CODE) == 0) {
		return true;
	}

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
