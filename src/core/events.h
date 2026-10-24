#ifndef ER_EVENTS_H
#define ER_EVENTS_H

#include "ereader_hal.h"

void er_queue_event(er_device *device, const struct er_event *event);
void er_input_reset(er_device *device);
void er_status_error(er_device *device, enum er_capability capability, int error);

#endif
