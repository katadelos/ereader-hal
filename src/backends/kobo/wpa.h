#ifndef ER_KOBO_WPA_H
#define ER_KOBO_WPA_H

#include "ereader_hal.h"

struct er_wpa {
    int fd, timeout_ms;
    bool uncertain;
    char directory[64], path[80], interface[16];
};

int er_wpa_open(const er_device *device, struct er_wpa *wpa);
void er_wpa_close(struct er_wpa *wpa);
int er_wpa_request(struct er_wpa *wpa, const char *command, char *reply, size_t size);
int er_wpa_ok(struct er_wpa *wpa, const char *command);
int er_wpa_receive(const struct er_wpa *wpa, char *reply, size_t size, int timeout_ms);
int er_wpa_persistence(const struct er_wpa *wpa);

#endif
