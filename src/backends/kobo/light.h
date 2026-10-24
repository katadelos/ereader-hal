#ifndef ER_KOBO_LIGHT_H
#define ER_KOBO_LIGHT_H
#include "ereader_hal.h"

int er_kobo_light_probe(const er_device *device, enum er_capability capability);
int er_kobo_light_get(const er_device *device, int *brightness, int *warmth);
int er_kobo_light_set(er_device *device, int brightness, int warmth);
#endif
