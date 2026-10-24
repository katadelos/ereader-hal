#define _GNU_SOURCE
#include "backends/network.h"
#include "backends/kindle/lipc_api.h"
#include "core/device.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static int operation_start(const er_device *d, enum er_capability cap, int timeout_ms,
                           struct er_wifi_result *result)
{
    if (!result)
        return -EINVAL;
    *result = (struct er_wifi_result){.completion = ER_WIFI_FAILED, .status.signal_dbm = INT_MIN};
    return timeout_ms < 0 ? -EINVAL : er_supported(d, cap);
}

static int operation_finish(struct er_wifi_result *result, int error)
{
    if (result) {
        result->error = -error;
        if (!error)
            result->completion = ER_WIFI_CONFIRMED;
    }
    return error;
}

int er_get_wifi_capabilities(const er_device *d, struct er_wifi_capabilities *caps)
{
    if (!caps)
        return -EINVAL;
    *caps = (struct er_wifi_capabilities){0};
    int result = er_supported(d, ER_WIFI_PROFILES);
    if (result)
        return result;
    caps->security_read = (1u << ER_WIFI_SECURITY_UNKNOWN) - 1;
    caps->security_write = (1u << ER_WIFI_OPEN) | (1u << ER_WIFI_WPA_PSK);
    caps->hidden = caps->enabled = caps->rename = d->vendor == ER_KOBO;
    return 0;
}

static int valid_id(const char *id)
{
    return id && id[0] && strnlen(id, ER_WIFI_ID_SIZE) < ER_WIFI_ID_SIZE ? 0 : -EINVAL;
}

static int valid_config(const struct er_wifi_config *config, bool adding)
{
    if (!config || !config->ssid[0] ||
        strnlen(config->ssid, ER_WIFI_SSID_SIZE) >= ER_WIFI_SSID_SIZE)
        return -EINVAL;
    if (config->security != ER_WIFI_OPEN && config->security != ER_WIFI_WPA_PSK &&
        config->security != ER_WIFI_SAE)
        return -EOPNOTSUPP;
    if (config->security == ER_WIFI_OPEN)
        return config->password && config->password[0] ? -EINVAL : 0;
    if (!config->password)
        return adding ? -EINVAL : 0;
    size_t length = strnlen(config->password, 65);
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)config->password[i] < 32 || config->password[i] == 127)
            return -EINVAL;
    if (config->security == ER_WIFI_SAE)
        return length >= 1 && length <= 63 ? 0 : -EINVAL;
    if (length >= 8 && length <= 63)
        return 0;
    if (length != 64)
        return -EINVAL;
    return strspn(config->password, "0123456789abcdefABCDEF") == 64 ? 0 : -EINVAL;
}

int er_get_wifi_status(er_device *d, struct er_wifi_status *status)
{
    if (!status)
        return -EINVAL;
    *status = (struct er_wifi_status){.signal_dbm = INT_MIN};
    int result = er_supported(d, ER_WIFI_STATUS);
    if (result)
        return result;
    return d->vendor == ER_KINDLE ? er_kindle_wifi_status(d, status)
                                  : er_kobo_wifi_status(d, status);
}

int er_wifi_list_networks(er_device *d, struct er_wifi_network *networks, size_t capacity,
                          size_t *count)
{
    if (!count || (capacity && !networks))
        return -EINVAL;
    *count = 0;
    int result = er_supported(d, ER_WIFI_PROFILES);
    if (result)
        return result;
    return d->vendor == ER_KINDLE ? er_kindle_wifi_list(d, networks, capacity, count)
                                  : er_kobo_wifi_list(d, networks, capacity, count);
}

int er_wifi_scan(er_device *d, struct er_wifi_access_point *points, size_t capacity, size_t *count)
{
    if (!count || (capacity && !points))
        return -EINVAL;
    *count = 0;
    int result = er_supported(d, ER_WIFI_SCAN);
    if (result)
        return result;
    return d->vendor == ER_KINDLE ? er_kindle_wifi_scan(d, points, capacity, count)
                                  : er_kobo_wifi_scan(d, points, capacity, count);
}

int er_wifi_add_network(er_device *d, const struct er_wifi_config *config, int timeout_ms,
                        struct er_wifi_result *out)
{
    int result = operation_start(d, ER_WIFI_PROFILES, timeout_ms, out);
    if (!result)
        result = valid_config(config, true);
    if (!result)
        result = d->vendor == ER_KINDLE ? er_kindle_wifi_add(d, config, timeout_ms, out)
                                        : er_kobo_wifi_add(d, config, timeout_ms, out);
    return operation_finish(out, result);
}

int er_wifi_update_network(er_device *d, const char *id, const struct er_wifi_config *config,
                           int timeout_ms, struct er_wifi_result *out)
{
    int result = operation_start(d, ER_WIFI_PROFILES, timeout_ms, out);
    if (!result)
        result = valid_id(id);
    if (!result)
        result = valid_config(config, false);
    if (!result)
        result = d->vendor == ER_KINDLE ? er_kindle_wifi_update(d, id, config, timeout_ms, out)
                                        : er_kobo_wifi_update(d, id, config, timeout_ms, out);
    return operation_finish(out, result);
}

int er_wifi_remove_network(er_device *d, const char *id, int timeout_ms, struct er_wifi_result *out)
{
    int result = operation_start(d, ER_WIFI_PROFILES, timeout_ms, out);
    if (!result)
        result = valid_id(id);
    if (!result)
        result = d->vendor == ER_KINDLE ? er_kindle_wifi_remove(d, id, timeout_ms, out)
                                        : er_kobo_wifi_remove(d, id, timeout_ms, out);
    return operation_finish(out, result);
}

int er_wifi_connect(er_device *d, const char *id, bool wait_for_address, int timeout_ms,
                    struct er_wifi_result *out)
{
    int result = operation_start(d, ER_WIFI_CONNECT, timeout_ms, out);
    if (!result)
        result = valid_id(id);
    if (!result)
        result = d->vendor == ER_KINDLE
                     ? er_kindle_wifi_connect(d, id, wait_for_address, timeout_ms, out)
                     : er_kobo_wifi_connect(d, id, wait_for_address, timeout_ms, out);
    return operation_finish(out, result);
}

int er_wifi_disconnect(er_device *d, int timeout_ms, struct er_wifi_result *out)
{
    int result = operation_start(d, ER_WIFI_CONNECT, timeout_ms, out);
    if (!result)
        result = d->vendor == ER_KINDLE ? er_kindle_wifi_disconnect(d, timeout_ms, out)
                                        : er_kobo_wifi_disconnect(d, timeout_ms, out);
    return operation_finish(out, result);
}

int er_wifi_wait_connection(er_device *d, const char *id, bool wait_for_address, int timeout_ms,
                            struct er_wifi_result *out)
{
    int result = operation_start(d, ER_WIFI_STATUS, timeout_ms, out);
    if (!result && id)
        result = valid_id(id);
    if (!result)
        result = d->vendor == ER_KINDLE
                     ? er_kindle_wifi_wait(d, id, wait_for_address, timeout_ms, out)
                     : er_kobo_wifi_wait(d, id, wait_for_address, timeout_ms, out);
    return operation_finish(out, result);
}

bool er_wifi_connection_matches(const struct er_wifi_status *status, const char *id,
                                bool wait_for_address)
{
    if (!id)
        return status->state == ER_WIFI_OFF || status->state == ER_WIFI_DISCONNECTED;
    return status->state == ER_WIFI_CONNECTED && !strcmp(status->network_id, id) &&
           (!wait_for_address || (status->address[0] && strcmp(status->address, "0.0.0.0") &&
                                  strcmp(status->address, "::")));
}

#define CMD "com.lab126.cmd"
#define WIFID "com.lab126.wifid"

int er_get_wifi(const er_device *d, bool *enabled)
{
    if (!enabled)
        return -EINVAL;
    int result = er_supported(d, ER_WIFI), raw;
    if (result)
        return result;
    if (d->vendor == ER_KOBO)
        return er_kobo_wifi_get(d, enabled);
    result = er_kindle_get(WIFID, "enable", &raw);
    if (result)
        return result;
    if (raw != 0 && raw != 1)
        return -EIO;
    *enabled = raw != 0;
    return 0;
}

int er_set_wifi(const er_device *d, bool enabled)
{
    int result = er_supported(d, ER_WIFI);
    if (result)
        return result;
    if (d->vendor == ER_KOBO)
        return er_kobo_wifi_set(d, enabled);
    if (d->profile->display_driver == ER_EINKFB)
        return er_kindle_set(WIFID, "enable", enabled);
    if (enabled)
        result = er_kindle_set(CMD, "wirelessEnable", 1);
    if (!result)
        result = er_kindle_set(WIFID, "enable", enabled);
    if (!result && !enabled)
        result = er_kindle_set(CMD, "wirelessEnable", 0);
    return result;
}
