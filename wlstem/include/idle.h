#ifndef WLSTEM_IDLE_H_
#define WLSTEM_IDLE_H_

enum sway_input_idle_source {
    IDLE_SOURCE_KEYBOARD = 1 << 0,
    IDLE_SOURCE_POINTER = 1 << 1,
    IDLE_SOURCE_TOUCH = 1 << 2,
    IDLE_SOURCE_TABLET_PAD = 1 << 3,
    IDLE_SOURCE_TABLET_TOOL = 1 << 4,
    IDLE_SOURCE_SWITCH = 1 << 5,
};

#endif /* WLSTEM_IDLE_H_ */
