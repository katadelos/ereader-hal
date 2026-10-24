#ifndef ER_KOBO_SETUP_H
#define ER_KOBO_SETUP_H
#include "ereader_hal.h"

enum er_wifi_power { ER_WIFI_POWER_MODULE, ER_WIFI_POWER_NTX, ER_WIFI_POWER_WMT };

int er_kobo_hardware_value(const char *codes, const char *field, char *value, size_t size);
void er_kobo_configure(er_device *device);
#endif
