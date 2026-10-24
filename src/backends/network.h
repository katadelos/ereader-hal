#ifndef ER_NETWORK_H
#define ER_NETWORK_H

#include "ereader_hal.h"
#include <stddef.h>

int er_kindle_network_probe(er_device *d, enum er_capability cap, struct er_capability_info *info);
int er_kindle_wifi_status(er_device *d, struct er_wifi_status *status);
int er_kindle_wifi_list(er_device *d, struct er_wifi_network *networks, size_t capacity,
                        size_t *count);
int er_kindle_wifi_scan(er_device *d, struct er_wifi_access_point *points, size_t capacity,
                        size_t *count);
int er_kindle_wifi_add(er_device *d, const struct er_wifi_config *config, int timeout_ms,
                       struct er_wifi_result *out);
int er_kindle_wifi_update(er_device *d, const char *id, const struct er_wifi_config *config,
                          int timeout_ms, struct er_wifi_result *out);
int er_kindle_wifi_remove(er_device *d, const char *id, int timeout_ms, struct er_wifi_result *out);
int er_kindle_wifi_connect(er_device *d, const char *id, bool wait_for_address, int timeout_ms,
                           struct er_wifi_result *out);
int er_kindle_wifi_disconnect(er_device *d, int timeout_ms, struct er_wifi_result *out);
int er_kindle_wifi_wait(er_device *d, const char *id, bool wait_for_address, int timeout_ms,
                        struct er_wifi_result *out);

int er_kobo_network_probe(const er_device *d, enum er_capability cap,
                          struct er_capability_info *info);
int er_kobo_wifi_status(const er_device *d, struct er_wifi_status *status);
int er_kobo_wifi_list(const er_device *d, struct er_wifi_network *networks, size_t capacity,
                      size_t *count);
int er_kobo_wifi_scan(const er_device *d, struct er_wifi_access_point *points, size_t capacity,
                      size_t *count);
int er_kobo_wifi_add(const er_device *d, const struct er_wifi_config *config, int timeout_ms,
                     struct er_wifi_result *out);
int er_kobo_wifi_update(const er_device *d, const char *id, const struct er_wifi_config *config,
                        int timeout_ms, struct er_wifi_result *out);
int er_kobo_wifi_remove(const er_device *d, const char *id, int timeout_ms,
                        struct er_wifi_result *out);
int er_kobo_wifi_connect(const er_device *d, const char *id, bool wait_for_address, int timeout_ms,
                         struct er_wifi_result *out);
int er_kobo_wifi_disconnect(const er_device *d, int timeout_ms, struct er_wifi_result *out);
int er_kobo_wifi_wait(const er_device *d, const char *id, bool wait_for_address, int timeout_ms,
                      struct er_wifi_result *out);

int er_kobo_wifi_get(const er_device *device, bool *enabled);
int er_kobo_wifi_set(const er_device *device, bool enabled);
int er_kobo_wifi_probe(const er_device *device, struct er_capability_info *info);
int er_kobo_wifi_suspend_blocker(const er_device *device, char *reason, size_t size);
bool er_wifi_connection_matches(const struct er_wifi_status *status, const char *id,
                                bool wait_for_address);
#endif
