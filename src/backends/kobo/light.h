#ifndef ER_KOBO_LIGHT_H
#define ER_KOBO_LIGHT_H
#include "ereader_hal.h"

void er_kobo_light_configure(er_device *device);
void er_kobo_light_changed(er_device *device, int brightness);
int er_light_prepare_suspend(er_device *device);

int er_kobo_light_probe(const er_device *device, enum er_capability capability);
int er_kobo_light_get(const er_device *device, int *brightness, int *warmth);
int er_kobo_light_set(er_device *device, int brightness, int warmth);
#endif
