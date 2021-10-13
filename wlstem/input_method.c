#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "input_method.h"
#include "log.h"

struct wls_input_method_manager *wls_input_method_manager_create(
        struct wl_display *display) {
    struct wls_input_method_manager *retval = calloc(1,
        sizeof(struct wls_input_method_manager));
    if (!retval) {
        sway_log(SWAY_ERROR, "failed to allocate input method manager");
        return NULL;
    }

    struct wlr_input_method_manager_v2 *_input_method =
        wlr_input_method_manager_v2_create(display);
    struct wlr_text_input_manager_v3 *_text_input =
        wlr_text_input_manager_v3_create(display);

    if (!_input_method || !_text_input) {
        sway_log(SWAY_ERROR, "failed to initialize input method manager fields");
        free(retval);
        return NULL;
    }
    retval->input_method = _input_method;
    retval->text_input = _text_input;
    return retval;
}
