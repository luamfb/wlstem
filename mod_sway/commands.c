#define _POSIX_C_SOURCE 200809
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <json.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/criteria.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/tree/view.h"
#include "stringop.h"
#include "log.h"

// Returns error object, or NULL if check succeeds.
struct cmd_results *checkarg(int argc, const char *name, enum expected_args type, int val) {
	const char *error_name = NULL;
	switch (type) {
	case EXPECTED_AT_LEAST:
		if (argc < val) {
			error_name = "at least ";
		}
		break;
	case EXPECTED_AT_MOST:
		if (argc > val) {
			error_name = "at most ";
		}
		break;
	case EXPECTED_EQUAL_TO:
		if (argc != val) {
			error_name = "";
		}
	}
	return error_name ?
		cmd_results_new(CMD_INVALID, "Invalid %s command "
				"(expected %s%d argument%s, got %d)",
				name, error_name, val, val != 1 ? "s" : "", argc)
		: NULL;
}

/* Keep alphabetized */
static struct cmd_handler handlers[] = {
	{ "bindcode", cmd_bindcode },
	{ "bindswitch", cmd_bindswitch },
	{ "bindsym", cmd_bindsym },
	{ "exec", cmd_exec },
	{ "exec_always", cmd_exec_always },
	{ "unbindcode", cmd_unbindcode },
	{ "unbindswitch", cmd_unbindswitch },
	{ "unbindsym", cmd_unbindsym },
};

/* Runtime-only commands. Keep alphabetized */
static struct cmd_handler command_handlers[] = {
	{ "resize", cmd_resize },
	{ "swap", cmd_swap },
};

static int handler_compare(const void *_a, const void *_b) {
	const struct cmd_handler *a = _a;
	const struct cmd_handler *b = _b;
	return strcasecmp(a->command, b->command);
}

struct cmd_handler *find_handler(char *line, struct cmd_handler *handlers,
		size_t handlers_size) {
	if (!handlers || !handlers_size) {
		return NULL;
	}
	struct cmd_handler query = { .command = line };
	return bsearch(&query, handlers,
			handlers_size / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
}

static struct cmd_handler *find_handler_ex(char *line,
		struct cmd_handler *command_handlers, size_t command_handlers_size,
		struct cmd_handler *handlers, size_t handlers_size) {
	struct cmd_handler *handler = NULL;
	if (config->active) {
		handler = find_handler(line, command_handlers, command_handlers_size);
	}
	return handler ? handler : find_handler(line, handlers, handlers_size);
}

static struct cmd_handler *find_core_handler(char *line) {
	return find_handler_ex(line,
			command_handlers, sizeof(command_handlers),
			handlers, sizeof(handlers));
}

static void set_config_node(struct sway_node *node) {
	config->handler_context.node = node;
	config->handler_context.container = NULL;
	config->handler_context.workspace = NULL;

	if (node == NULL) {
		return;
	}

	switch (node->type) {
	case N_CONTAINER:
		config->handler_context.container = node->sway_container;
		config->handler_context.workspace = node->sway_container->workspace;
		break;
	case N_WORKSPACE:
		config->handler_context.workspace = node->sway_workspace;
		break;
	case N_ROOT:
	case N_OUTPUT:
		break;
	}
}

list_t *execute_command(char *_exec, struct sway_seat *seat,
		struct sway_container *con) {
	char *cmd;
	char matched_delim = ';';
	list_t *containers = NULL;

	if (seat == NULL) {
		// passing a NULL seat means we just pick the default seat
		seat = input_manager_get_default_seat();
		if (!sway_assert(seat, "could not find a seat to run the command on")) {
			return NULL;
		}
	}

	char *exec = strdup(_exec);
	char *head = exec;
	list_t *res_list = create_list();

	if (!res_list || !exec) {
		return NULL;
	}

	config->handler_context.seat = seat;

	do {
		for (; isspace(*head); ++head) {}
		// Extract criteria (valid for this command list only).
		if (matched_delim == ';') {
			config->handler_context.using_criteria = false;
			if (*head == '[') {
				char *error = NULL;
				struct criteria *criteria = criteria_parse(head, &error);
				if (!criteria) {
					list_add(res_list,
							cmd_results_new(CMD_INVALID, "%s", error));
					free(error);
					goto cleanup;
				}
				list_free(containers);
				containers = criteria_get_containers(criteria);
				head += strlen(criteria->raw);
				criteria_destroy(criteria);
				config->handler_context.using_criteria = true;
				// Skip leading whitespace
				for (; isspace(*head); ++head) {}
			}
		}
		// Split command list
		cmd = argsep(&head, ";,", &matched_delim);
		for (; isspace(*cmd); ++cmd) {}

		if (strcmp(cmd, "") == 0) {
			sway_log(SWAY_INFO, "Ignoring empty command.");
			continue;
		}
		sway_log(SWAY_INFO, "Handling command '%s'", cmd);
		//TODO better handling of argv
		int argc;
		char **argv = split_args(cmd, &argc);
		if (strcmp(argv[0], "exec") != 0 &&
				strcmp(argv[0], "exec_always") != 0 &&
				strcmp(argv[0], "mode") != 0) {
			for (int i = 1; i < argc; ++i) {
				if (*argv[i] == '\"' || *argv[i] == '\'') {
					strip_quotes(argv[i]);
				}
			}
		}
		struct cmd_handler *handler = find_core_handler(argv[0]);
		if (!handler) {
			list_add(res_list, cmd_results_new(CMD_INVALID,
					"Unknown/invalid command '%s'", argv[0]));
			free_argv(argc, argv);
			goto cleanup;
		}

		if (!config->handler_context.using_criteria) {
			// The container or workspace which this command will run on.
			struct sway_node *node = con ? &con->node :
					seat_get_focus_inactive(seat, &root->node);
			set_config_node(node);
			struct cmd_results *res = handler->handle(argc-1, argv+1);
			list_add(res_list, res);
			if (res->status == CMD_INVALID) {
				free_argv(argc, argv);
				goto cleanup;
			}
		} else if (containers->length == 0) {
			list_add(res_list,
					cmd_results_new(CMD_FAILURE, "No matching node."));
		} else {
			struct cmd_results *fail_res = NULL;
			for (int i = 0; i < containers->length; ++i) {
				struct sway_container *container = containers->items[i];
				set_config_node(&container->node);
				struct cmd_results *res = handler->handle(argc-1, argv+1);
				if (res->status == CMD_SUCCESS) {
					free_cmd_results(res);
				} else {
					// last failure will take precedence
					if (fail_res) {
						free_cmd_results(fail_res);
					}
					fail_res = res;
					if (res->status == CMD_INVALID) {
						list_add(res_list, fail_res);
						free_argv(argc, argv);
						goto cleanup;
					}
				}
			}
			list_add(res_list,
					fail_res ? fail_res : cmd_results_new(CMD_SUCCESS, NULL));
		}
		free_argv(argc, argv);
	} while(head);
cleanup:
	free(exec);
	list_free(containers);
	return res_list;
}

// this is like execute_command above, except:
// 1) it ignores empty commands (empty lines)
// 2) it does variable substitution
// 3) it doesn't split commands (because the multiple commands are supposed to
//	  be chained together)
// 4) execute_command handles all state internally while config_command has
// some state handled outside (notably the block mode, in read_config)
struct cmd_results *config_command(char *exec, char **new_block) {
	struct cmd_results *results = NULL;
	int argc;
	char **argv = split_args(exec, &argc);

	// Check for empty lines
	if (!argc) {
		results = cmd_results_new(CMD_SUCCESS, NULL);
		goto cleanup;
	}

	// Check for the start of a block
	if (argc > 1 && strcmp(argv[argc - 1], "{") == 0) {
		*new_block = join_args(argv, argc - 1);
		results = cmd_results_new(CMD_BLOCK, NULL);
		goto cleanup;
	}

	// Check for the end of a block
	if (strcmp(argv[argc - 1], "}") == 0) {
		results = cmd_results_new(CMD_BLOCK_END, NULL);
		goto cleanup;
	}

	// Determine the command handler
	sway_log(SWAY_INFO, "Config command: %s", exec);
	struct cmd_handler *handler = find_core_handler(argv[0]);
	if (!handler || !handler->handle) {
		const char *error = handler
			? "Command '%s' is shimmed, but unimplemented"
			: "Unknown/invalid command '%s'";
		results = cmd_results_new(CMD_INVALID, error, argv[0]);
		goto cleanup;
	}

	char *command = join_args(argv, argc);
	sway_log(SWAY_INFO, "After replacement: %s", command);
	free_argv(argc, argv);
	argv = split_args(command, &argc);
	free(command);

	// Strip quotes and unescape the string
	for (int i = 1; i < argc; ++i) {
		if (handler->handle != cmd_exec && handler->handle != cmd_exec_always
				&& handler->handle != cmd_bindsym
				&& handler->handle != cmd_bindcode
				&& handler->handle != cmd_bindswitch
				&& (*argv[i] == '\"' || *argv[i] == '\'')) {
			strip_quotes(argv[i]);
		}
		unescape_string(argv[i]);
	}

	// Run command
	results = handler->handle(argc - 1, argv + 1);

cleanup:
	free_argv(argc, argv);
	return results;
}

struct cmd_results *config_subcommand(char **argv, int argc,
		struct cmd_handler *handlers, size_t handlers_size) {
	char *command = join_args(argv, argc);
	sway_log(SWAY_DEBUG, "Subcommand: %s", command);
	free(command);

	struct cmd_handler *handler = find_handler(argv[0], handlers,
			handlers_size);
	if (!handler) {
		return cmd_results_new(CMD_INVALID,
				"Unknown/invalid command '%s'", argv[0]);
	}
	if (handler->handle) {
		return handler->handle(argc - 1, argv + 1);
	}
	return cmd_results_new(CMD_INVALID,
			"The command '%s' is shimmed, but unimplemented", argv[0]);
}

struct cmd_results *config_commands_command(char *exec) {
	struct cmd_results *results = NULL;
	int argc;
	char **argv = split_args(exec, &argc);
	if (!argc) {
		results = cmd_results_new(CMD_SUCCESS, NULL);
		goto cleanup;
	}

	// Find handler for the command this is setting a policy for
	char *cmd = argv[0];

	if (strcmp(cmd, "}") == 0) {
		results = cmd_results_new(CMD_BLOCK_END, NULL);
		goto cleanup;
	}

	struct cmd_handler *handler = find_handler(cmd, NULL, 0);
	if (!handler && strcmp(cmd, "*") != 0) {
		results = cmd_results_new(CMD_INVALID,
			"Unknown/invalid command '%s'", cmd);
		goto cleanup;
	}

	enum command_context context = 0;

	struct {
		char *name;
		enum command_context context;
	} context_names[] = {
		{ "config", CONTEXT_CONFIG },
		{ "binding", CONTEXT_BINDING },
		{ "ipc", CONTEXT_IPC },
		{ "criteria", CONTEXT_CRITERIA },
		{ "all", CONTEXT_ALL },
	};

	for (int i = 1; i < argc; ++i) {
		size_t j;
		for (j = 0; j < sizeof(context_names) / sizeof(context_names[0]); ++j) {
			if (strcmp(context_names[j].name, argv[i]) == 0) {
				break;
			}
		}
		if (j == sizeof(context_names) / sizeof(context_names[0])) {
			results = cmd_results_new(CMD_INVALID,
					"Invalid command context %s", argv[i]);
			goto cleanup;
		}
		context |= context_names[j].context;
	}

	results = cmd_results_new(CMD_SUCCESS, NULL);

cleanup:
	free_argv(argc, argv);
	return results;
}

struct cmd_results *cmd_results_new(enum cmd_status status,
		const char *format, ...) {
	struct cmd_results *results = malloc(sizeof(struct cmd_results));
	if (!results) {
		sway_log(SWAY_ERROR, "Unable to allocate command results");
		return NULL;
	}
	results->status = status;
	if (format) {
		char *error = malloc(256);
		va_list args;
		va_start(args, format);
		if (error) {
			vsnprintf(error, 256, format, args);
		}
		va_end(args);
		results->error = error;
	} else {
		results->error = NULL;
	}
	return results;
}

void free_cmd_results(struct cmd_results *results) {
	if (results->error) {
		free(results->error);
	}
	free(results);
}

char *cmd_results_to_json(list_t *res_list) {
	json_object *result_array = json_object_new_array();
	for (int i = 0; i < res_list->length; ++i) {
		struct cmd_results *results = res_list->items[i];
		json_object *root = json_object_new_object();
		json_object_object_add(root, "success",
				json_object_new_boolean(results->status == CMD_SUCCESS));
		if (results->error) {
			json_object_object_add(root, "parse_error",
					json_object_new_boolean(results->status == CMD_INVALID));
			json_object_object_add(
					root, "error", json_object_new_string(results->error));
		}
		json_object_array_add(result_array, root);
	}
	const char *json = json_object_to_json_string(result_array);
	char *res = strdup(json);
	json_object_put(result_array);
	return res;
}
