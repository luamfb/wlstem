#ifndef _SWAY_COMMANDS_H
#define _SWAY_COMMANDS_H

#include <wlr/util/edges.h>
#include "config.h"

struct sway_container;

typedef struct cmd_results *sway_cmd(int argc, char **argv);

struct cmd_handler {
    char *command;
    sway_cmd *handle;
};

/**
 * Indicates the result of a command's execution.
 */
enum cmd_status {
    CMD_SUCCESS,         /**< The command was successful */
    CMD_FAILURE,        /**< The command resulted in an error */
    CMD_INVALID,         /**< Unknown command or parser error */
    CMD_DEFER,        /**< Command execution deferred */
    CMD_BLOCK,
    CMD_BLOCK_COMMANDS,
    CMD_BLOCK_END
};

/**
 * Stores the result of executing a command.
 */
struct cmd_results {
    enum cmd_status status;
    /**
     * Human friendly error message, or NULL on success
     */
    char *error;
};

enum expected_args {
    EXPECTED_AT_LEAST,
    EXPECTED_AT_MOST,
    EXPECTED_EQUAL_TO
};

struct cmd_results *checkarg(int argc, const char *name,
        enum expected_args type, int val);

struct cmd_handler *find_handler(char *line, struct cmd_handler *cmd_handlers,
        size_t handlers_size);

/**
 * Parse and executes a command.
 *
 * If the command string contains criteria then the command will be executed on
 * all matching containers. Otherwise, it'll run on the `con` container. If
 * `con` is NULL then it'll run on the currently focused container.
 */
list_t *execute_command(char *command,  struct sway_seat *seat,
        struct sway_container *con);
/**
 * Parse and handles a command during config file loading.
 *
 * Do not use this under normal conditions.
 */
struct cmd_results *config_command(char *command, char **new_block);
/**
 * Parse and handle a sub command
 */
struct cmd_results *config_subcommand(char **argv, int argc,
        struct cmd_handler *handlers, size_t handlers_size);
/*
 * Parses a command policy rule.
 */
struct cmd_results *config_commands_command(char *exec);
/**
 * Allocates a cmd_results object.
 */
struct cmd_results *cmd_results_new(enum cmd_status status, const char *error, ...);
/**
 * Frees a cmd_results object.
 */
void free_cmd_results(struct cmd_results *results);
/**
 * Serializes a list of cmd_results to a JSON string.
 *
 * Free the JSON string later on.
 */
char *cmd_results_to_json(list_t *res_list);

/**
 * TODO: Move this function and its dependent functions to container.c.
 */
void container_resize_tiled(struct sway_container *parent, uint32_t axis,
        int amount);

struct sway_container *container_find_resize_parent(struct sway_container *con,
        uint32_t edge);

/**
 * Handlers shared by exec and exec_always.
 */
sway_cmd cmd_exec_validate;
sway_cmd cmd_exec_process;

sway_cmd cmd_bindcode;
sway_cmd cmd_bindswitch;
sway_cmd cmd_bindsym;
sway_cmd cmd_exec;
sway_cmd cmd_exec_always;
sway_cmd cmd_resize;
sway_cmd cmd_swap;
sway_cmd cmd_unbindcode;
sway_cmd cmd_unbindswitch;
sway_cmd cmd_unbindsym;

#endif
