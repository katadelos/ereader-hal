#define _POSIX_C_SOURCE 200809L
#include "backends/network.h"
#include "linux/io.h"
#include "lipc_api.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIFID "com.lab126.wifid"

struct kindle_network {
    struct er_lipc api;
    char *properties;
    bool numeric_ids;
};

static bool has_property(const struct kindle_network *network, const char *name, const char *type)
{
    const char *text = network->properties;
    char property[64], property_type[16], access[8];
    int consumed;
    while (sscanf(text, "%63s %15s %7s %n", property, property_type, access, &consumed) == 3) {
        if (!strcmp(name, property))
            return !strcmp(type, property_type);
        text += consumed;
    }
    return false;
}

static void close_network(struct kindle_network *network)
{
    if (network->properties)
        network->api.free_string(network->properties);
    er_lipc_close(&network->api);
}

static int open_network(struct kindle_network *network)
{
    memset(network, 0, sizeof(*network));
    int result = er_lipc_open(&network->api, true);
    if (result)
        return result;
    struct er_lipc *api = &network->api;
    result =
        er_lipc_error(api->get_string(api->handle, WIFID, "_properties", &network->properties));
    if (result) {
        close_network(network);
        return result;
    }
    network->numeric_ids = has_property(network, "deleteProfile", "Int");
    return 0;
}

static int copy_text(char *output, size_t size, const char *value)
{
    size_t length = strlen(value);
    if (length >= size)
        return -EOVERFLOW;
    memcpy(output, value, length + 1);
    return 0;
}

static int get_text(struct er_lipc *api, const char *property, char *output, size_t size)
{
    char *value = NULL;
    int result = er_lipc_error(api->get_string(api->handle, WIFID, property, &value));
    if (!result)
        result = copy_text(output, size, value);
    if (value)
        api->free_string(value);
    return result;
}

static const char *hash_text(struct er_lipc *api, void *hash, int index, const char *key)
{
    char *value = NULL;
    return api->get_hash_string(hash, index, key, &value) ? "" : value;
}

static enum er_wifi_security get_security(struct er_lipc *api, void *hash, int index)
{
    const char *method = hash_text(api, hash, index, "smethod");
    const char *key = hash_text(api, hash, index, "key_mgmt");
    if (strstr(key, "EAP") || !strcmp(hash_text(api, hash, index, "enterprise"), "yes"))
        return ER_WIFI_ENTERPRISE;
    if (strstr(key, "SAE") || strstr(method, "wpa3"))
        return ER_WIFI_SAE;
    if (strstr(key, "PSK") || strstr(method, "wpa"))
        return ER_WIFI_WPA_PSK;
    if (strstr(key, "WEP") || !strcmp(method, "wep"))
        return ER_WIFI_WEP;
    if (!strcmp(hash_text(api, hash, index, "secured"), "no"))
        return ER_WIFI_OPEN;
    return ER_WIFI_SECURITY_UNKNOWN;
}

static int profile_id(struct kindle_network *network, void *hash, int index, char *id)
{
    struct er_lipc *api = &network->api;
    if (network->numeric_ids) {
        int number;
        int result = er_lipc_error(api->get_hash_int(hash, index, "netid", &number));
        if (result)
            return result;
        if (number < 0) {
            id[0] = '\0';
            return 0;
        }
        snprintf(id, ER_WIFI_ID_SIZE, "netid:%d", number);
    } else {
        const char *ssid = hash_text(api, hash, index, "essid");
        size_t length = strlen(ssid);
        if (length >= ER_WIFI_SSID_SIZE)
            return -EOVERFLOW;
        if (!length) {
            id[0] = '\0';
            return 0;
        }
        memcpy(id, "ssid:", 5);
        for (size_t i = 0; i < length; i++)
            snprintf(id + 5 + 2 * i, 3, "%02x", (unsigned char)ssid[i]);
    }
    return 0;
}

static int current_id(struct kindle_network *network, char *id)
{
    void *hash = NULL;
    int result = er_lipc_read_hash(&network->api, WIFID, "currentEssid", &hash);
    if (!result) {
        if (network->api.hash_count(hash) > 0)
            result = profile_id(network, hash, 0, id);
        network->api.destroy_hash(hash);
    }
    return result;
}

static int require_radio(struct er_lipc *api)
{
    int enabled;
    int result = er_lipc_error(api->get_int(api->handle, WIFID, "enable", &enabled));
    return result ? result : enabled ? 0 : -EAGAIN;
}

static int require_capability(struct kindle_network *network, enum er_capability cap)
{
    int result = 0;
    bool present = false;
    switch (cap) {
    case ER_WIFI_STATUS:
        present =
            has_property(network, "cmState", "Str") && has_property(network, "currentEssid", "Has");
        break;
    case ER_WIFI_SCAN:
        present = has_property(network, "scan", "Str") &&
                  has_property(network, "scanState", "Str") &&
                  has_property(network, "scanList", "Has");
        break;
    case ER_WIFI_PROFILES:
        present = has_property(network, "profileData", "Has") &&
                  has_property(network, "createProfile", "Has") &&
                  (network->numeric_ids || has_property(network, "deleteProfile", "Str"));
        break;
    case ER_WIFI_CONNECT:
        present = has_property(network, "cmConnect", "Str") &&
                  has_property(network, "cmDisconnect", "Str");
        break;
    default:
        break;
    }
    if (!present) {
        result = -EOPNOTSUPP;

    } else if (cap == ER_WIFI_SCAN || cap == ER_WIFI_CONNECT) {
        result = require_radio(&network->api);
    }
    return result;
}

static int open_operation(struct kindle_network *network, enum er_capability cap)
{
    int result = open_network(network);
    if (!result) {
        result = require_capability(network, cap);
        if (result)
            close_network(network);
    }
    return result;
}

int er_kindle_network_probe(er_device *d, enum er_capability cap, struct er_capability_info *info)
{
    (void)d;
    struct kindle_network network;
    int result = open_operation(&network, cap);
    if (!result)
        close_network(&network);
    else {
        info->reason_code = result == -EAGAIN ? ER_REASON_RADIO_OFF : ER_REASON_SERVICE_UNAVAILABLE;
        snprintf(info->reason,
                 sizeof(info->reason),
                 "%s",
                 result == -EAGAIN ? "Wi-Fi radio is off"
                                   : "Required Kindle Wi-Fi service properties are unavailable");
    }
    return result;
}

static int network_status(struct kindle_network *network, struct er_wifi_status *status)
{
    struct er_lipc *api = &network->api;
    int result;
    memset(status, 0, sizeof(*status));
    status->signal_dbm = INT_MIN;
    int enabled;
    result = er_lipc_error(api->get_int(api->handle, WIFID, "enable", &enabled));
    if (!result && enabled) {
        char state[64];
        result = get_text(api, "cmState", state, sizeof(state));
        if (!result) {
            if (!strcmp(state, "CONNECTED") || !strcmp(state, "CONNECTED_LOCAL"))
                status->state = ER_WIFI_CONNECTED;
            else if (!strcmp(state, "PENDING"))
                status->state = ER_WIFI_CONNECTING;
            else
                status->state = ER_WIFI_DISCONNECTED;
        }
    }
    void *hash = NULL;
    if (!result && status->state == ER_WIFI_CONNECTED) {
        result = er_lipc_read_hash(api, WIFID, "currentEssid", &hash);
        if (!result && api->hash_count(hash) > 0) {
            result = profile_id(network, hash, 0, status->network_id);
            if (!result)
                result =
                    copy_text(status->ssid, sizeof(status->ssid), hash_text(api, hash, 0, "essid"));
            if (!result)
                result = copy_text(
                    status->bssid, sizeof(status->bssid), hash_text(api, hash, 0, "bssid"));
        }
        if (hash)
            api->destroy_hash(hash);
        hash = NULL;
        if (!result) {
            result = er_lipc_read_hash(api, WIFID, "cmIntfInfo", &hash);
            if (!result && api->hash_count(hash) > 0)
                result = copy_text(
                    status->address, sizeof(status->address), hash_text(api, hash, 0, "cmIpAddr"));
            if (hash)
                api->destroy_hash(hash);
        }
    }
    return result;
}

int er_kindle_wifi_status(er_device *d, struct er_wifi_status *status)
{
    (void)d;
    struct kindle_network network;
    int result = open_operation(&network, ER_WIFI_STATUS);
    if (!result) {
        result = network_status(&network, status);
        close_network(&network);
    }
    return result;
}

static int wait_connection(struct kindle_network *network, const char *id, bool address,
                           int timeout_ms, struct er_wifi_result *out)
{
    int64_t deadline = er_now_ms() + timeout_ms;
    out->completion = ER_WIFI_UNCERTAIN;
    for (;;) {
        int result = network_status(network, &out->status);
        if (result)
            return result;
        out->status_known = true;
        if (er_wifi_connection_matches(&out->status, id, address))
            return 0;
        int64_t remaining = deadline - er_now_ms();
        if (remaining <= 0)
            return -ETIMEDOUT;
        result = er_sleep_ms(remaining < 100 ? (int)remaining : 100);
        if (result)
            return result;
    }
}

int er_kindle_wifi_wait(er_device *d, const char *id, bool address, int timeout_ms,
                        struct er_wifi_result *out)
{
    (void)d;
    struct kindle_network network;
    int result = open_operation(&network, ER_WIFI_STATUS);
    if (!result) {
        if (id)
            snprintf(out->network_id, sizeof(out->network_id), "%s", id);
        result = wait_connection(&network, id, address, timeout_ms, out);
        close_network(&network);
    }
    return result;
}

int er_kindle_wifi_list(er_device *d, struct er_wifi_network *networks, size_t capacity,
                        size_t *count)
{
    (void)d;
    struct kindle_network network;
    int result = open_operation(&network, ER_WIFI_PROFILES);
    if (result)
        return result;
    struct er_lipc *api = &network.api;
    void *hash = NULL;
    result = er_lipc_read_hash(api, WIFID, "profileData", &hash);
    char current[ER_WIFI_ID_SIZE] = "";
    if (!result)
        result = current_id(&network, current);
    if (!result) {
        int total = api->hash_count(hash);
        if (total < 0)
            result = -EIO;
        else {
            *count = (size_t)total;
            for (size_t i = 0; !result && i < *count && i < capacity; i++) {
                struct er_wifi_network *item = &networks[i];
                memset(item, 0, sizeof(*item));
                result = profile_id(&network, hash, (int)i, item->id);
                if (!result)
                    result = copy_text(
                        item->ssid, sizeof(item->ssid), hash_text(api, hash, (int)i, "essid"));
                item->security = get_security(api, hash, (int)i);
                item->enabled = true;
                item->current = current[0] && !strcmp(current, item->id);
            }
            if (!result && *count > capacity)
                result = -ENOSPC;
        }
    }
    if (hash)
        api->destroy_hash(hash);
    close_network(&network);
    return result;
}

int er_kindle_wifi_scan(er_device *d, struct er_wifi_access_point *points, size_t capacity,
                        size_t *count)
{
    (void)d;
    struct kindle_network network;
    int result = open_operation(&network, ER_WIFI_SCAN);
    if (result)
        return result;
    struct er_lipc *api = &network.api;
    result = require_radio(api);
    if (!result)
        result = er_lipc_error(api->set_string(api->handle, WIFID, "scan", ""));
    char state[32];
    bool complete = false;
    int64_t deadline = er_now_ms() + 20000;
    while (!result && er_now_ms() < deadline) {
        result = er_sleep_ms(250);
        if (!result)
            result = get_text(api, "scanState", state, sizeof(state));
        if (!result && !strcmp(state, "idle")) {
            complete = true;
            break;
        }
    }
    if (!result && !complete)
        result = -ETIMEDOUT;
    void *hash = NULL;
    if (!result)
        result = er_lipc_read_hash(api, WIFID, "scanList", &hash);
    if (!result) {
        int total = api->hash_count(hash);
        if (total < 0)
            result = -EIO;
        else {
            *count = (size_t)total;
            for (size_t i = 0; !result && i < *count && i < capacity; i++) {
                struct er_wifi_access_point *item = &points[i];
                memset(item, 0, sizeof(*item));
                item->signal_dbm = INT_MIN;
                item->security = get_security(api, hash, (int)i);
                result = copy_text(
                    item->ssid, sizeof(item->ssid), hash_text(api, hash, (int)i, "essid"));
                if (!result)
                    result = copy_text(
                        item->bssid, sizeof(item->bssid), hash_text(api, hash, (int)i, "bssid"));
            }
            if (!result && *count > capacity)
                result = -ENOSPC;
        }
    }
    if (hash)
        api->destroy_hash(hash);
    close_network(&network);
    return result;
}

static int find_profile(struct kindle_network *network, void *hash, const char *id)
{
    int count = network->api.hash_count(hash);
    if (count < 0)
        return -EIO;
    for (int i = 0; i < count; i++) {
        char candidate[ER_WIFI_ID_SIZE];
        int result = profile_id(network, hash, i, candidate);
        if (result)
            return result;
        if (!strcmp(candidate, id))
            return i;
    }
    return -ENOENT;
}

static int matching_profile(struct kindle_network *network, void *hash,
                            const struct er_wifi_config *config)
{
    struct er_lipc *api = &network->api;
    int count = api->hash_count(hash);
    if (count < 0)
        return -EIO;
    for (int i = 0; i < count; i++) {
        if (!strcmp(hash_text(api, hash, i, "essid"), config->ssid) &&
            (!network->numeric_ids || get_security(api, hash, i) == config->security))
            return i;
    }
    return -ENOENT;
}

enum profile_change { PROFILE_ADDED, PROFILE_UPDATED, PROFILE_REMOVED };

static int wait_profile(struct kindle_network *network, enum profile_change change, const char *id,
                        const struct er_wifi_config *config, char *created_id, int timeout_ms)
{
    struct er_lipc *api = &network->api;
    int64_t deadline = er_now_ms() + timeout_ms;
    for (;;) {
        void *hash = NULL;
        bool finished = false;
        int result = er_lipc_read_hash(api, WIFID, "profileData", &hash);
        if (!result) {
            int index = change == PROFILE_ADDED ? matching_profile(network, hash, config)
                                                : find_profile(network, hash, id);
            if (index == -ENOENT && change != PROFILE_UPDATED)
                finished = change == PROFILE_REMOVED;
            else if (index < 0)
                result = index;
            else if (change == PROFILE_ADDED) {
                result = profile_id(network, hash, index, created_id);
                finished = !result;
            } else if (change == PROFILE_UPDATED)
                finished = !strcmp(hash_text(api, hash, index, "psk"), config->password);
        }
        if (hash)
            api->destroy_hash(hash);
        if (finished)
            return 0;
        if (result && result != -ETIMEDOUT && result != -EAGAIN)
            return result;
        int64_t remaining = deadline - er_now_ms();
        if (remaining <= 0)
            return -ETIMEDOUT;
        result = er_sleep_ms(remaining < 100 ? (int)remaining : 100);
        if (result)
            return result;
    }
}

static int check_config(const struct er_wifi_config *config)
{
    if (config->security != ER_WIFI_OPEN && config->security != ER_WIFI_WPA_PSK)
        return -EOPNOTSUPP;
    if (config->hidden || !config->enabled)
        return -EOPNOTSUPP;
    return 0;
}

static int save_profile(struct kindle_network *network, const struct er_wifi_config *config,
                        const char *password, const char *method, void *previous,
                        int previous_index, struct er_wifi_result *out)
{
    struct er_lipc *api = &network->api;
    if (config->security == ER_WIFI_WPA_PSK && (!password || !password[0]))
        return -EINVAL;
    void *input = api->new_hash(api->handle), *output = NULL;
    if (!input)
        return -ENOMEM;
    size_t index;
    int result = er_lipc_error(api->add_hash(input, &index));
    if (!result)
        result = er_lipc_error(api->put_hash_string(input, 0, "essid", config->ssid));
    if (!result)
        result = er_lipc_error(api->put_hash_string(
            input, 0, "secured", config->security == ER_WIFI_OPEN ? "no" : "yes"));
    if (!result && config->security == ER_WIFI_WPA_PSK)
        result = er_lipc_error(api->put_hash_string(input, 0, "smethod", method));
    if (!result && config->security == ER_WIFI_WPA_PSK)
        result = er_lipc_error(api->put_hash_string(input, 0, "psk", password));
    const char *proxy_keys[] = {"os_proxy_host",
                                "os_proxy_port",
                                "os_proxy_excluded_hosts",
                                "wrs_proxy_host",
                                "wrs_proxy_port"};
    for (size_t i = 0; !result && previous && i < sizeof(proxy_keys) / sizeof(proxy_keys[0]); i++) {
        const char *value = hash_text(api, previous, previous_index, proxy_keys[i]);
        result = er_lipc_error(api->put_hash_string(input, 0, proxy_keys[i], value));
    }
    if (!result)
        result = er_lipc_error(api->put_hash_int(input, 0, "store_nw_user_pref", 0));
    if (!result) {
        out->completion = ER_WIFI_UNCERTAIN;
        result =
            er_lipc_error(api->access_hash(api->handle, WIFID, "createProfile", input, &output));
    }
    if (output)
        api->destroy_hash(output);
    api->destroy_hash(input);
    return result;
}

int er_kindle_wifi_add(er_device *d, const struct er_wifi_config *config, int timeout_ms,
                       struct er_wifi_result *out)
{
    (void)d;
    int result = check_config(config);
    if (result)
        return result;
    struct kindle_network network;
    result = open_operation(&network, ER_WIFI_PROFILES);
    if (result)
        return result;
    void *hash = NULL;
    result = er_lipc_read_hash(&network.api, WIFID, "profileData", &hash);
    if (!result) {
        int found = matching_profile(&network, hash, config);
        result = found >= 0 ? -EEXIST : found == -ENOENT ? 0 : found;
        network.api.destroy_hash(hash);
    }
    if (!result) {
        result = save_profile(&network, config, config->password, "wpa2", NULL, 0, out);
        if (!result || result == -ETIMEDOUT)
            result =
                wait_profile(&network, PROFILE_ADDED, NULL, config, out->network_id, timeout_ms);
        else if (result == -EINVAL || result == -EOPNOTSUPP || result == -EACCES)
            out->completion = ER_WIFI_FAILED;
    }
    close_network(&network);
    return result;
}

int er_kindle_wifi_update(er_device *d, const char *id, const struct er_wifi_config *config,
                          int timeout_ms, struct er_wifi_result *out)
{
    (void)d;
    int result = check_config(config);
    if (result)
        return result;
    struct kindle_network network;
    result = open_operation(&network, ER_WIFI_PROFILES);
    if (result)
        return result;
    struct er_lipc *api = &network.api;
    void *hash = NULL;
    result = er_lipc_read_hash(api, WIFID, "profileData", &hash);
    if (!result) {
        int index = find_profile(&network, hash, id);
        if (index < 0)
            result = index;
        else if (strcmp(config->ssid, hash_text(api, hash, index, "essid")) ||
                 config->security != get_security(api, hash, index))
            result = -EOPNOTSUPP;
        else if (config->password && config->security == ER_WIFI_WPA_PSK) {
            result = save_profile(&network,
                                  config,
                                  config->password,
                                  hash_text(api, hash, index, "smethod"),
                                  hash,
                                  index,
                                  out);
            if (result == -EINVAL || result == -EOPNOTSUPP || result == -EACCES)
                out->completion = ER_WIFI_FAILED;
        }
    }
    if (hash)
        api->destroy_hash(hash);
    snprintf(out->network_id, sizeof(out->network_id), "%s", id);
    if ((!result || (result == -ETIMEDOUT && out->completion == ER_WIFI_UNCERTAIN)) &&
        config->password && config->security != ER_WIFI_OPEN)
        result = wait_profile(&network, PROFILE_UPDATED, id, config, NULL, timeout_ms);
    close_network(&network);
    return result;
}

static int operate_profile(const char *id, bool connect, bool address, int timeout_ms,
                           struct er_wifi_result *out)
{
    struct kindle_network network;
    int result = open_operation(&network, connect ? ER_WIFI_CONNECT : ER_WIFI_PROFILES);
    if (result)
        return result;
    struct er_lipc *api = &network.api;
    void *hash = NULL;
    result = er_lipc_read_hash(api, WIFID, "profileData", &hash);
    if (!result) {
        int index = find_profile(&network, hash, id);
        if (index < 0)
            result = index;
        else if (network.numeric_ids) {
            int number;
            result = er_lipc_error(api->get_hash_int(hash, index, "netid", &number));
            if (!result && connect) {
                out->completion = ER_WIFI_UNCERTAIN;
                char value[32];
                snprintf(value, sizeof(value), "%d", number);
                result = er_lipc_error(api->set_string(api->handle, WIFID, "cmConnect", value));
            } else if (!result) {
                out->completion = ER_WIFI_UNCERTAIN;
                result = er_lipc_error(api->set_int(api->handle, WIFID, "deleteProfile", number));
            }
        } else {
            out->completion = ER_WIFI_UNCERTAIN;
            const char *ssid = hash_text(api, hash, index, "essid");
            result = er_lipc_error(
                api->set_string(api->handle, WIFID, connect ? "cmConnect" : "deleteProfile", ssid));
        }
    }
    if (hash)
        api->destroy_hash(hash);
    snprintf(out->network_id, sizeof(out->network_id), "%s", id);
    if (out->completion == ER_WIFI_UNCERTAIN &&
        (result == -EINVAL || result == -EACCES || result == -EOPNOTSUPP))
        out->completion = ER_WIFI_FAILED;
    if (!result || (result == -ETIMEDOUT && out->completion == ER_WIFI_UNCERTAIN))
        result = connect ? wait_connection(&network, id, address, timeout_ms, out)
                         : wait_profile(&network, PROFILE_REMOVED, id, NULL, NULL, timeout_ms);
    close_network(&network);
    return result;
}

int er_kindle_wifi_remove(er_device *d, const char *id, int timeout_ms, struct er_wifi_result *out)
{
    (void)d;
    return operate_profile(id, false, false, timeout_ms, out);
}

int er_kindle_wifi_connect(er_device *d, const char *id, bool address, int timeout_ms,
                           struct er_wifi_result *out)
{
    (void)d;
    return operate_profile(id, true, address, timeout_ms, out);
}

int er_kindle_wifi_disconnect(er_device *d, int timeout_ms, struct er_wifi_result *out)
{
    (void)d;
    struct kindle_network network;
    int result = open_network(&network);
    if (result)
        return result;
    result = network_status(&network, &out->status);
    out->status_known = !result;
    if (!result && out->status.state != ER_WIFI_OFF && out->status.state != ER_WIFI_DISCONNECTED) {
        out->completion = ER_WIFI_UNCERTAIN;
        result =
            er_lipc_error(network.api.set_string(network.api.handle, WIFID, "cmDisconnect", ""));
        if (!result || result == -ETIMEDOUT)
            result = wait_connection(&network, NULL, false, timeout_ms, out);
        else if (result == -EINVAL || result == -EOPNOTSUPP || result == -EACCES)
            out->completion = ER_WIFI_FAILED;
    }
    close_network(&network);
    return result;
}
