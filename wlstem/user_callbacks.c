#include <stdbool.h>
#include "log.h"
#include "user_callbacks.h"

bool validate_callbacks(const struct wls_user_callbacks *callbacks) {
    if (!callbacks) {
        sway_log(SWAY_ERROR, "NULL struct wls_user_callbacks pointer");
        return false;
    }

    if (!callbacks->handle_output_commit
            || !callbacks->output_render_overlay
            || !callbacks->output_render_non_overlay
            || !callbacks->choose_absorber_output) {
        sway_log(SWAY_ERROR, "NULL callback in wls_user_callbacks struct");
        return false;
    }

    return true;
}
