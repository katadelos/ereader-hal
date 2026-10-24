#ifndef EREADER_HAL_WIFI_H
#define EREADER_HAL_WIFI_H
#include "ereader_hal_types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ER_WIFI_SSID_SIZE 33
#define ER_WIFI_ID_SIZE 128

enum er_wifi_security {
    ER_WIFI_OPEN,
    ER_WIFI_WPA_PSK,
    ER_WIFI_SAE,
    ER_WIFI_WEP,
    ER_WIFI_ENTERPRISE,
    ER_WIFI_SECURITY_UNKNOWN
};
enum er_wifi_state { ER_WIFI_OFF, ER_WIFI_DISCONNECTED, ER_WIFI_CONNECTING, ER_WIFI_CONNECTED };
struct er_wifi_network {
    char id[ER_WIFI_ID_SIZE];
    char ssid[ER_WIFI_SSID_SIZE];
    enum er_wifi_security security;
    bool hidden, enabled, current, hidden_known, enabled_known;
};
struct er_wifi_config {
    char ssid[ER_WIFI_SSID_SIZE];
    enum er_wifi_security security;
    bool hidden, enabled;
    const char *password; /* Write-only. NULL preserves credentials on update. */
};
struct er_wifi_access_point {
    char ssid[ER_WIFI_SSID_SIZE], bssid[18];
    enum er_wifi_security security;
    int signal_dbm; /* INT_MIN when not reported. */
    unsigned frequency_mhz;
};
struct er_wifi_status {
    enum er_wifi_state state;
    char network_id[ER_WIFI_ID_SIZE];
    char ssid[ER_WIFI_SSID_SIZE], bssid[18], address[46];
    int signal_dbm; /* INT_MIN when not reported. */
};

enum er_wifi_completion { ER_WIFI_CONFIRMED, ER_WIFI_FAILED, ER_WIFI_UNCERTAIN };
struct er_wifi_result {
    enum er_wifi_completion completion;
    int error; /* Positive errno; zero when confirmed. */
    char network_id[ER_WIFI_ID_SIZE];
    bool status_known;
    struct er_wifi_status status;
};
struct er_wifi_capabilities {
    uint32_t security_read, security_write; /* Bits indexed by er_wifi_security. */
    bool hidden, enabled, rename;
};

int er_get_wifi(const er_device *device, bool *enabled);
int er_set_wifi(const er_device *device, bool enabled);
int er_get_wifi_status(er_device *device, struct er_wifi_status *status);
int er_get_wifi_capabilities(const er_device *device, struct er_wifi_capabilities *capabilities);
/* List functions set count to the required size; insufficient capacity returns -ENOSPC. */
int er_wifi_list_networks(er_device *device, struct er_wifi_network *networks, size_t capacity,
                          size_t *count);
int er_wifi_scan(er_device *device, struct er_wifi_access_point *points, size_t capacity,
                 size_t *count);
/* Mutations persist profiles. IDs are opaque; list again after reconnect/reboot.
 * timeout_ms bounds completion polling and Kobo mutation acknowledgements.
 * Zero performs an immediate completion read; command acknowledgements retain their default
 * timeout. Native LIPC calls have their own service timeout. Uncertain results must be reconciled
 * through reads, without replaying the mutation.
 */
int er_wifi_add_network(er_device *device, const struct er_wifi_config *config, int timeout_ms,
                        struct er_wifi_result *result);
int er_wifi_update_network(er_device *device, const char *id, const struct er_wifi_config *config,
                           int timeout_ms, struct er_wifi_result *result);
int er_wifi_remove_network(er_device *device, const char *id, int timeout_ms,
                           struct er_wifi_result *result);
int er_wifi_connect(er_device *device, const char *id, bool wait_for_address, int timeout_ms,
                    struct er_wifi_result *result);
int er_wifi_disconnect(er_device *device, int timeout_ms, struct er_wifi_result *result);
/* A NULL ID waits for a disconnected or powered-off radio without issuing commands. */
int er_wifi_wait_connection(er_device *device, const char *id, bool wait_for_address,
                            int timeout_ms, struct er_wifi_result *result);

#ifdef __cplusplus
}
#endif
#endif
