#include "sway_config.h"
#include "transaction.h"
#include "sway_switch.h"
#include <wlr/types/wlr_idle.h>
#include "log.h"

struct sway_switch *sway_switch_create(struct sway_seat *seat,
        struct sway_seat_device *device) {
    struct sway_switch *switch_device =
        calloc(1, sizeof(struct sway_switch));
    if (!sway_assert(switch_device, "could not allocate switch")) {
        return NULL;
    }
    device->switch_device = switch_device;
    switch_device->seat_device = device;
    switch_device->state = WLR_SWITCH_STATE_OFF;
    wl_list_init(&switch_device->switch_toggle.link);
    sway_log(SWAY_DEBUG, "Allocated switch for device");

    return switch_device;
}

static void handle_switch_toggle(struct wl_listener *listener, void *data) {
    struct sway_switch *sway_switch =
            wl_container_of(listener, sway_switch, switch_toggle);
    struct wlr_event_switch_toggle *event = data;
    struct sway_seat *seat = sway_switch->seat_device->sway_seat;
    seat_idle_notify_activity(seat, IDLE_SOURCE_SWITCH);

    struct wlr_input_device *wlr_device =
        sway_switch->seat_device->input_device->wlr_device;
    char *device_identifier = input_device_get_identifier(wlr_device);
    sway_log(SWAY_DEBUG, "%s: type %d state %d", device_identifier,
            event->switch_type, event->switch_state);
    free(device_identifier);

    sway_switch->type = event->switch_type;
    sway_switch->state = event->switch_state;
}

void sway_switch_configure(struct sway_switch *sway_switch) {
    struct wlr_input_device *wlr_device =
        sway_switch->seat_device->input_device->wlr_device;
    wl_list_remove(&sway_switch->switch_toggle.link);
    wl_signal_add(&wlr_device->switch_device->events.toggle,
            &sway_switch->switch_toggle);
    sway_switch->switch_toggle.notify = handle_switch_toggle;
    sway_log(SWAY_DEBUG, "Configured switch for device");
}

void sway_switch_destroy(struct sway_switch *sway_switch) {
    if (!sway_switch) {
        return;
    }
    wl_list_remove(&sway_switch->switch_toggle.link);
    free(sway_switch);
}
