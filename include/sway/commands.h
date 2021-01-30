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

bool checkarg(int argc, const char *name,
        enum expected_args type, int val);

#endif
