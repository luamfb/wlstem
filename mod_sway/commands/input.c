#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/input/input-manager.h"
#include "sway/input/keyboard.h"
#include "log.h"
#include "stringop.h"

// must be in order for the bsearch
static struct cmd_handler input_handlers[] = {
	{ "map_from_region", input_cmd_map_from_region },
	{ "map_to_region", input_cmd_map_to_region },
	{ "tool_mode", input_cmd_tool_mode },
	{ "xkb_file", input_cmd_xkb_file },
	{ "xkb_switch_layout", input_cmd_xkb_switch_layout },
};

// must be in order for the bsearch
static struct cmd_handler input_config_handlers[] = {
};

struct cmd_results *cmd_input(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "input", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	sway_log(SWAY_DEBUG, "entering input block: %s", argv[0]);

	config->handler_context.input_config = new_input_config(argv[0]);
	if (!config->handler_context.input_config) {
		return cmd_results_new(CMD_FAILURE, "Couldn't allocate config");
	}

	struct cmd_results *res;

	if (find_handler(argv[1], input_config_handlers,
			sizeof(input_config_handlers))) {
        res = cmd_results_new(CMD_FAILURE, "Can only be used in config file.");
	} else {
		res = config_subcommand(argv + 1, argc - 1,
			input_handlers, sizeof(input_handlers));
	}

	if ((!res || res->status == CMD_SUCCESS) &&
			strcmp(argv[1], "xkb_switch_layout") != 0) {
		char *error = NULL;
		struct input_config *ic =
			store_input_config(config->handler_context.input_config, &error);
		if (!ic) {
			free_input_config(config->handler_context.input_config);
			if (res) {
				free_cmd_results(res);
			}
			res = cmd_results_new(CMD_FAILURE, "Failed to compile keymap: %s",
					error ? error : "(details unavailable)");
			free(error);
			return res;
		}
	} else {
		free_input_config(config->handler_context.input_config);
	}

	config->handler_context.input_config = NULL;

	return res;
}
