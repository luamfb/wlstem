#ifndef _SWAY_INPUT_LIBINPUT_H
#define _SWAY_INPUT_LIBINPUT_H
#include "input_manager.h"

void sway_input_configure_libinput_device(struct sway_input_device *device);

void sway_input_reset_libinput_device(struct sway_input_device *device);

#endif
