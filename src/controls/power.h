#ifndef ER_POWER_H
#define ER_POWER_H
#include "ereader_hal.h"

int er_rtc_path(char *path, size_t size, const char *attribute);
int er_suspend_blocker(const er_device *device, char *reason, size_t size);
int er_awake_probe(const er_device *device, char *reason, size_t size);
#endif
