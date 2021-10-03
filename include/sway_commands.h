#ifndef _SWAY_COMMANDS_H
#define _SWAY_COMMANDS_H

#include <wlr/util/edges.h>
#include "config.h"

enum expected_args {
    EXPECTED_AT_LEAST,
    EXPECTED_AT_MOST,
    EXPECTED_EQUAL_TO
};

bool checkarg(int argc, const char *name,
        enum expected_args type, int val);

#endif
