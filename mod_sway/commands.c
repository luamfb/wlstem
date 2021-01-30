#define _POSIX_C_SOURCE 200809
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/tree/view.h"
#include "stringop.h"
#include "log.h"

bool checkarg(int argc, const char *name, enum expected_args type, int val) {
	switch (type) {
	case EXPECTED_AT_LEAST:
		if (argc < val) {
			return false;
		}
		break;
	case EXPECTED_AT_MOST:
		if (argc > val) {
			return false;
		}
		break;
	case EXPECTED_EQUAL_TO:
		if (argc != val) {
			return false;
		}
	}
	return true;
}
