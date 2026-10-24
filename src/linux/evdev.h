#ifndef ER_EVDEV_H
#define ER_EVDEV_H

#include "ereader_hal.h"
#include <linux/input.h>

#define ER_INPUT_COUNT 8
#define ER_CONTACT_COUNT 16

struct er_contact {
    int id, x, y;
    bool active;
};
struct er_input {
    int fd, error;
    char path[64];
    bool touch, slots, single, dropped, touch_down;
    int slot;
    struct input_absinfo x_axis, y_axis;
    struct er_contact current[ER_CONTACT_COUNT], previous[ER_CONTACT_COUNT];
};

int er_input_open(er_device *device);
int er_input_capability_probe(er_device *device, enum er_capability capability);
void er_input_close(er_device *device);
void er_input_clear(er_device *device);
int er_input_poll(er_device *device, int timeout_ms);

#endif
