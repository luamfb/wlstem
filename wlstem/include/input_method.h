#ifndef WLSTEM_INPUT_METHOD_H_
#define WLSTEM_INPUT_METHOD_H_
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_text_input_v3.h>

struct wls_input_method_manager {
    struct wlr_input_method_manager_v2 *input_method;
    struct wlr_text_input_manager_v3 *text_input;
};

struct wls_input_method_manager *wls_input_method_manager_create(
    struct wl_display *display);

#endif /* WLSTEM_INPUT_METHOD_H_ */
