#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_damage.h>
#include "foreach.h"
#include "output.h"
#include "seat.h"
#include "sway_server.h"
#include "log.h"
#include "util.h"
#include "wlstem.h"

struct sway_output * choose_absorber_output(struct sway_output *giver) {
    struct sway_output *absorber = NULL;
    if (wls->output_manager->outputs->length > 1) {
        absorber = wls->output_manager->outputs->items[0];
        if (absorber == giver) {
            absorber = wls->output_manager->outputs->items[1];
        }
    }
    return absorber;
}
