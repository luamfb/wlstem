#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"
#include "list.h"
#include "log.h"

// must be in order for the bsearch
static struct cmd_handler output_handlers[] = {
	{ "background", output_cmd_background },
	{ "bg", output_cmd_background },
};

struct cmd_results *cmd_output(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "output", EXPECTED_AT_LEAST, 1);
	if (error != NULL) {
		return error;
	}

	// The NOOP-1 output is a dummy output used when there's no outputs
	// connected. It should never be configured.
	if (strcasecmp(argv[0], root->noop_output->wlr_output->name) == 0) {
		return cmd_results_new(CMD_FAILURE,
				"Refusing to configure the no op output");
	}

	struct output_config *output = NULL;
	if (strcmp(argv[0], "-") == 0 || strcmp(argv[0], "--") == 0) {
		struct sway_output *sway_output = config->handler_context.node ?
			node_get_output(config->handler_context.node) : NULL;
		if (!sway_output) {
			return cmd_results_new(CMD_FAILURE, "Unknown output");
		}
		if (sway_output == root->noop_output) {
			return cmd_results_new(CMD_FAILURE,
					"Refusing to configure the no op output");
		}
		if (strcmp(argv[0], "-") == 0) {
			output = new_output_config(sway_output->wlr_output->name);
		} else {
			char identifier[128];
			output_get_identifier(identifier, 128, sway_output);
			output = new_output_config(identifier);
		}
	} else {
		output = new_output_config(argv[0]);
	}
	if (!output) {
		sway_log(SWAY_ERROR, "Failed to allocate output config");
		return NULL;
	}
	argc--; argv++;

	config->handler_context.output_config = output;

	while (argc > 0) {
		config->handler_context.leftovers.argc = 0;
		config->handler_context.leftovers.argv = NULL;

		if (find_handler(*argv, output_handlers, sizeof(output_handlers))) {
			error = config_subcommand(argv, argc, output_handlers,
					sizeof(output_handlers));
		} else {
			error = cmd_results_new(CMD_INVALID,
				"Invalid output subcommand: %s.", *argv);
		}

		if (error != NULL) {
			goto fail;
		}

		argc = config->handler_context.leftovers.argc;
		argv = config->handler_context.leftovers.argv;
	}

	config->handler_context.output_config = NULL;
	config->handler_context.leftovers.argc = 0;
	config->handler_context.leftovers.argv = NULL;

	output = store_output_config(output);

	return cmd_results_new(CMD_SUCCESS, NULL);

fail:
	config->handler_context.output_config = NULL;
	free_output_config(output);
	return error;
}
