#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "output_config.h"

void free_output_config(struct output_config *oc) {
    if (!oc) {
        return;
    }
    free(oc->name);
    free(oc->background);
    free(oc->background_option);
    free(oc);
}
