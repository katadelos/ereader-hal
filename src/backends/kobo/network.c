#define _GNU_SOURCE
#include "backends/network.h"
#include "linux/io.h"
#include "wpa.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static int open_network(const er_device *d, struct er_wpa *wpa)
{
    int result = er_wpa_open(d, wpa);
    if (result == -ENOENT || result == -ECONNREFUSED) {
        bool enabled;
        if (!er_kobo_wifi_get(d, &enabled) && !enabled)
            return -EAGAIN;
    }
    return result;
}

static int network_id(const char *id)
{
    if (!id || !*id)
        return -EINVAL;
    for (const char *p = id; *p; ++p)
        if (!isdigit((unsigned char)*p))
            return -EINVAL;
    int value;
    return er_parse_int(id, &value) || value < 0 ? -EINVAL : 0;
}

static int field(const char *text, const char *name, char *value, size_t size)
{
    size_t length = strlen(name);
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);
        if (!strncmp(line, name, length) && line[length] == '=') {
            size_t count = (size_t)(end - line) - length - 1;
            if (count >= size)
                return -EOVERFLOW;
            memcpy(value, line + length + 1, count);
            value[count] = '\0';
            return 0;
        }
        line = *end ? end + 1 : end;
    }
    return -ENOENT;
}

static int decode_ssid(const char *text, char output[ER_WIFI_SSID_SIZE])
{
    size_t count = 0;
    while (*text) {
        unsigned char value = (unsigned char)*text++;
        if (value == '\\' && *text) {
            value = (unsigned char)*text++;
            if (value == 'x') {
                if (!isxdigit((unsigned char)text[0]) || !isxdigit((unsigned char)text[1]))
                    return -EPROTO;
                const char hex[3] = {text[0], text[1], '\0'};
                value = (unsigned char)strtoul(hex, NULL, 16);
                text += 2;
            } else if (value == 'n') {
                value = '\n';
            } else if (value == 'r') {
                value = '\r';
            } else if (value == 't') {
                value = '\t';
            } else if (value == 'e') {
                value = 27;
            }
        }
        if (!value)
            return -EILSEQ;
        if (count >= ER_WIFI_SSID_SIZE - 1)
            return -EOVERFLOW;
        output[count++] = (char)value;
    }
    output[count] = '\0';
    return 0;
}

static enum er_wifi_security security(const char *flags)
{
    if (strstr(flags, "EAP") || strstr(flags, "IEEE8021X"))
        return ER_WIFI_ENTERPRISE;
    if (strstr(flags, "PSK"))
        return ER_WIFI_WPA_PSK;
    if (strstr(flags, "SAE"))
        return ER_WIFI_SAE;
    if (strstr(flags, "WEP"))
        return ER_WIFI_WEP;
    if (!strcmp(flags, "NONE") || (strstr(flags, "[ESS]") && !strstr(flags, "WPA") &&
                                   !strstr(flags, "RSN") && !strstr(flags, "OWE")))
        return ER_WIFI_OPEN;
    return ER_WIFI_SECURITY_UNKNOWN;
}

int er_kobo_network_probe(const er_device *d, enum er_capability cap,
                          struct er_capability_info *info)
{
    struct er_wpa wpa;
    int result = open_network(d, &wpa);
    if (result == -EAGAIN && cap == ER_WIFI_STATUS)
        return 0;
    if (!result) {
        char reply[32];
        result = er_wpa_request(&wpa, "PING", reply, sizeof(reply));
        if (!result && strcmp(reply, "PONG"))
            result = -EPROTO;
        if (!result && cap == ER_WIFI_PROFILES)
            result = er_wpa_persistence(&wpa);
        if (!result && cap == ER_WIFI_CONNECT && access("/sbin/dhcpcd", X_OK))
            result = -errno;
        er_wpa_close(&wpa);
    }
    if (result) {
        info->reason_code = result == -EAGAIN  ? ER_REASON_RADIO_OFF
                            : result == -EROFS ? ER_REASON_PERMISSION
                                               : ER_REASON_SERVICE_UNAVAILABLE;
        snprintf(info->reason,
                 sizeof(info->reason),
                 "%s",
                 result == -EAGAIN  ? "Wi-Fi is off"
                 : result == -EROFS ? "Supplicant configuration is not writable"
                                    : "Supplicant control interface is unavailable");
    }
    return result;
}

static int network_status(struct er_wpa *wpa, struct er_wifi_status *status)
{
    *status = (struct er_wifi_status){.state = ER_WIFI_DISCONNECTED, .signal_dbm = INT_MIN};
    char reply[4096], value[256];
    int result = er_wpa_request(wpa, "STATUS", reply, sizeof(reply));
    if (!result) {
        if (!field(reply, "wpa_state", value, sizeof(value))) {
            if (!strcmp(value, "COMPLETED"))
                status->state = ER_WIFI_CONNECTED;
            else if (strcmp(value, "DISCONNECTED") && strcmp(value, "INACTIVE") &&
                     strcmp(value, "INTERFACE_DISABLED"))
                status->state = ER_WIFI_CONNECTING;
        }
        field(reply, "id", status->network_id, sizeof(status->network_id));
        field(reply, "bssid", status->bssid, sizeof(status->bssid));
        field(reply, "ip_address", status->address, sizeof(status->address));
        if (!field(reply, "ssid", value, sizeof(value)))
            result = decode_ssid(value, status->ssid);
    }
    if (!result && status->state == ER_WIFI_CONNECTED &&
        !er_wpa_request(wpa, "SIGNAL_POLL", reply, sizeof(reply)) &&
        !field(reply, "RSSI", value, sizeof(value)))
        er_parse_int(value, &status->signal_dbm);
    return result;
}

int er_kobo_wifi_status(const er_device *d, struct er_wifi_status *status)
{
    *status = (struct er_wifi_status){.state = ER_WIFI_OFF, .signal_dbm = INT_MIN};
    struct er_wpa wpa;
    int result = open_network(d, &wpa);
    if (result == -EAGAIN)
        return 0;
    if (!result) {
        result = network_status(&wpa, status);
        er_wpa_close(&wpa);
    }
    return result;
}

static int wait_connection(struct er_wpa *wpa, const char *id, bool address, int timeout_ms,
                           struct er_wifi_result *out)
{
    int64_t deadline = er_now_ms() + timeout_ms;
    out->completion = ER_WIFI_UNCERTAIN;
    for (;;) {
        int64_t remaining = deadline - er_now_ms();
        if (timeout_ms)
            wpa->timeout_ms = remaining > 0 ? (int)remaining : 1;
        int result = network_status(wpa, &out->status);
        if (result)
            return result;
        out->status_known = true;
        if (er_wifi_connection_matches(&out->status, id, address))
            return 0;
        remaining = deadline - er_now_ms();
        if (remaining <= 0)
            return -ETIMEDOUT;
        result = er_sleep_ms(remaining < 100 ? (int)remaining : 100);
        if (result)
            return result;
    }
}

int er_kobo_wifi_wait(const er_device *d, const char *id, bool address, int timeout_ms,
                      struct er_wifi_result *out)
{
    struct er_wpa wpa;
    int result = open_network(d, &wpa);
    if (result == -EAGAIN && !id) {
        out->status.state = ER_WIFI_OFF;
        out->status_known = true;
        return 0;
    }
    if (!result) {
        if (id)
            snprintf(out->network_id, sizeof(out->network_id), "%s", id);
        result = wait_connection(&wpa, id, address, timeout_ms, out);
        er_wpa_close(&wpa);
    }
    return result;
}

static int get_network(struct er_wpa *wpa, const char *id, const char *name, char *value,
                       size_t size)
{
    char command[192];
    snprintf(command, sizeof(command), "GET_NETWORK %s %s", id, name);
    return er_wpa_request(wpa, command, value, size);
}

int er_kobo_wifi_list(const er_device *d, struct er_wifi_network *networks, size_t capacity,
                      size_t *count)
{
    *count = 0;
    struct er_wpa wpa;
    int result = open_network(d, &wpa);
    if (result)
        return result;
    char reply[65536];
    result = er_wpa_request(&wpa, "LIST_NETWORKS", reply, sizeof(reply));
    char *line = result ? NULL : strchr(reply, '\n');
    while (line && *++line) {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        char *columns = line;
        const char *id = strsep(&columns, "\t");
        const char *ssid = strsep(&columns, "\t");
        strsep(&columns, "\t"); /* BSSID restriction */
        if (!ssid || !columns || network_id(id)) {
            result = -EPROTO;
            break;
        }
        struct er_wifi_network network = {0};
        snprintf(network.id, sizeof(network.id), "%s", id);
        result = decode_ssid(ssid, network.ssid);
        if (result)
            break;
        network.current = strstr(columns, "[CURRENT]") != NULL;
        network.enabled = strstr(columns, "[DISABLED]") == NULL;
        char value[256];
        result = get_network(&wpa, id, "key_mgmt", value, sizeof(value));
        if (result)
            break;
        network.security = security(value);
        if (network.security == ER_WIFI_OPEN &&
            !get_network(&wpa, id, "wep_key0", value, sizeof(value)))
            network.security = ER_WIFI_WEP;
        result = get_network(&wpa, id, "scan_ssid", value, sizeof(value));
        if (result)
            break;
        network.hidden = !strcmp(value, "1");
        network.hidden_known = true;
        network.enabled_known = true;
        if (*count < capacity)
            networks[*count] = network;
        ++*count;
        line = end;
    }
    er_wpa_close(&wpa);
    return result ? result : *count > capacity ? -ENOSPC : 0;
}

static int scan_wait(const er_device *d, struct er_wpa *wpa)
{
    struct er_wpa monitor;
    int result = er_wpa_open(d, &monitor);
    if (result)
        return result;
    result = er_wpa_ok(&monitor, "ATTACH");
    if (!result)
        result = er_wpa_ok(wpa, "SCAN");
    int64_t deadline = er_now_ms() + 15000;
    while (!result) {
        char event[1024];
        int remaining = (int)(deadline - er_now_ms());
        result =
            remaining > 0 ? er_wpa_receive(&monitor, event, sizeof(event), remaining) : -ETIMEDOUT;
        if (result || strstr(event, "CTRL-EVENT-SCAN-RESULTS"))
            break;
        if (strstr(event, "CTRL-EVENT-SCAN-FAILED"))
            result = -EIO;
    }
    er_wpa_close(&monitor);
    return result;
}

int er_kobo_wifi_scan(const er_device *d, struct er_wifi_access_point *points, size_t capacity,
                      size_t *count)
{
    *count = 0;
    struct er_wpa wpa;
    int result = open_network(d, &wpa);
    if (result)
        return result;
    result = scan_wait(d, &wpa);
    for (unsigned index = 0; !result; ++index) {
        char command[32], reply[8192], value[256];
        snprintf(command, sizeof(command), "BSS %u", index);
        result = er_wpa_request(&wpa, command, reply, sizeof(reply));
        if (result == -EINVAL) {
            result = 0;
            break;
        }
        if (result)
            break;
        struct er_wifi_access_point point = {.signal_dbm = INT_MIN};
        if (field(reply, "ssid", value, sizeof(value)) || decode_ssid(value, point.ssid) ||
            field(reply, "bssid", point.bssid, sizeof(point.bssid)) ||
            field(reply, "flags", value, sizeof(value))) {
            result = -EPROTO;
            break;
        }
        point.security = security(value);
        if (!field(reply, "level", value, sizeof(value)))
            er_parse_int(value, &point.signal_dbm);
        int frequency;
        if (!field(reply, "freq", value, sizeof(value)) && !er_parse_int(value, &frequency) &&
            frequency > 0)
            point.frequency_mhz = (unsigned)frequency;
        if (*count < capacity)
            points[*count] = point;
        ++*count;
    }
    er_wpa_close(&wpa);
    return result ? result : *count > capacity ? -ENOSPC : 0;
}

static int mutation_open(const er_device *d, struct er_wpa *wpa, int *lock)
{
    *lock = open("/var/run/ereader-hal-network.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (*lock < 0)
        return -errno;
    if (flock(*lock, LOCK_EX | LOCK_NB)) {
        int result = errno == EWOULDBLOCK ? -EBUSY : -errno;
        close(*lock);
        return result;
    }
    int result = open_network(d, wpa);
    if (!result) {
        result = er_wpa_persistence(wpa);
        if (result)
            er_wpa_close(wpa);
    }
    if (result)
        close(*lock);
    return result;
}

static int set_network(struct er_wpa *wpa, const char *id, const char *name, const char *value)
{
    char command[320];
    int length = snprintf(command, sizeof(command), "SET_NETWORK %s %s %s", id, name, value);
    if (length >= (int)sizeof(command))
        return -EOVERFLOW;
    return er_wpa_ok(wpa, command);
}

static int configure(struct er_wpa *wpa, const char *id, const struct er_wifi_config *config)
{
    char ssid[65];
    size_t length = strlen(config->ssid);
    for (size_t i = 0; i < length; ++i)
        snprintf(ssid + i * 2, 3, "%02x", (unsigned char)config->ssid[i]);
    int result = set_network(wpa, id, "ssid", ssid);
    if (!result)
        result =
            set_network(wpa, id, "key_mgmt", config->security == ER_WIFI_OPEN ? "NONE" : "WPA-PSK");
    if (!result)
        result = set_network(wpa, id, "scan_ssid", config->hidden ? "1" : "0");
    if (!result && config->security == ER_WIFI_WPA_PSK && config->password) {
        char password[67];
        if (strlen(config->password) == 64)
            snprintf(password, sizeof(password), "%s", config->password);
        else
            snprintf(password, sizeof(password), "\"%s\"", config->password);
        result = set_network(wpa, id, "psk", password);
    }
    if (!result)
        result = set_network(wpa, id, "disabled", config->enabled ? "0" : "1");
    return result;
}

static int mutation_finish(struct er_wpa *wpa, int lock, int result, struct er_wifi_result *out)
{
    if (!result)
        result = er_wpa_ok(wpa, "SAVE_CONFIG");
    if (result && !wpa->uncertain) {
        /* A rejected edit can be rolled back to the last saved profiles. */
        if (!er_wpa_ok(wpa, "RECONFIGURE"))
            out->completion = ER_WIFI_FAILED;
        else
            out->completion = ER_WIFI_UNCERTAIN;
    }
    er_wpa_close(wpa);
    close(lock);
    return result;
}

int er_kobo_wifi_add(const er_device *d, const struct er_wifi_config *config, int timeout_ms,
                     struct er_wifi_result *out)
{
    if (config->security != ER_WIFI_OPEN && config->security != ER_WIFI_WPA_PSK)
        return -ENOTSUP;
    struct er_wpa wpa;
    int lock;
    int result = mutation_open(d, &wpa, &lock);
    if (result)
        return result;
    wpa.timeout_ms = timeout_ms ? timeout_ms : 3000;
    out->completion = ER_WIFI_UNCERTAIN;
    char created[ER_WIFI_ID_SIZE];
    result = er_wpa_request(&wpa, "ADD_NETWORK", created, sizeof(created));
    if (!result)
        result = network_id(created);
    if (!result) {
        snprintf(out->network_id, sizeof(out->network_id), "%s", created);
        result = configure(&wpa, created, config);
    }
    result = mutation_finish(&wpa, lock, result, out);
    return result;
}

int er_kobo_wifi_update(const er_device *d, const char *id, const struct er_wifi_config *config,
                        int timeout_ms, struct er_wifi_result *out)
{
    if (network_id(id))
        return -EINVAL;
    if (config->security != ER_WIFI_OPEN && config->security != ER_WIFI_WPA_PSK)
        return -ENOTSUP;
    struct er_wpa wpa;
    int lock;
    int result = mutation_open(d, &wpa, &lock);
    if (result)
        return result;
    char value[256];
    result = get_network(&wpa, id, "key_mgmt", value, sizeof(value));
    enum er_wifi_security previous = result ? ER_WIFI_SECURITY_UNKNOWN : security(value);
    if (!result && previous == ER_WIFI_OPEN &&
        !get_network(&wpa, id, "wep_key0", value, sizeof(value)))
        previous = ER_WIFI_WEP;
    if (!result && previous != ER_WIFI_OPEN && previous != ER_WIFI_WPA_PSK)
        result = -ENOTSUP;
    if (!result && previous != config->security && !config->password &&
        config->security != ER_WIFI_OPEN)
        result = -EINVAL;
    if (result) {
        er_wpa_close(&wpa);
        close(lock);
        return result;
    }
    wpa.timeout_ms = timeout_ms ? timeout_ms : 3000;
    snprintf(out->network_id, sizeof(out->network_id), "%s", id);
    out->completion = ER_WIFI_UNCERTAIN;
    result = configure(&wpa, id, config);
    return mutation_finish(&wpa, lock, result, out);
}

int er_kobo_wifi_remove(const er_device *d, const char *id, int timeout_ms,
                        struct er_wifi_result *out)
{
    if (network_id(id))
        return -EINVAL;
    struct er_wpa wpa;
    int lock;
    int result = mutation_open(d, &wpa, &lock);
    if (result)
        return result;
    wpa.timeout_ms = timeout_ms ? timeout_ms : 3000;
    snprintf(out->network_id, sizeof(out->network_id), "%s", id);
    out->completion = ER_WIFI_UNCERTAIN;
    char command[160];
    snprintf(command, sizeof(command), "REMOVE_NETWORK %s", id);
    result = er_wpa_ok(&wpa, command);
    return mutation_finish(&wpa, lock, result, out);
}

static bool dhcp_stopped(void)
{
    glob_t processes = {0};
    bool stopped = glob("/proc/[0-9]*/comm", 0, NULL, &processes) == 0;
    for (size_t i = 0; stopped && i < processes.gl_pathc; ++i) {
        char name[64];
        int result = er_read_line(processes.gl_pathv[i], name, sizeof(name));
        if (result == -ENOENT)
            continue;
        if (result || !strcmp(name, "dhcpcd"))
            stopped = false;
    }
    globfree(&processes);
    return stopped;
}

static int change_connection(const er_device *d, const char *id, bool address, int timeout_ms,
                             struct er_wifi_result *out)
{
    struct er_wpa wpa;
    int result = open_network(d, &wpa);
    if (result == -EAGAIN && !id) {
        out->status.state = ER_WIFI_OFF;
        out->status_known = true;
        return 0;
    }
    if (result)
        return result;
    if (access("/sbin/dhcpcd", X_OK)) {
        result = -errno;
        er_wpa_close(&wpa);
        return result;
    }
    wpa.timeout_ms = timeout_ms ? timeout_ms : 3000;
    char command[160];
    if (id) {
        snprintf(out->network_id, sizeof(out->network_id), "%s", id);
        snprintf(command, sizeof(command), "SELECT_NETWORK %s", id);
    } else
        snprintf(command, sizeof(command), "DISCONNECT");
    out->completion = ER_WIFI_UNCERTAIN;
    result = er_wpa_ok(&wpa, command);
    if (result && !wpa.uncertain)
        out->completion = ER_WIFI_FAILED;
    if (!result) {
        char *connect[] = {"/sbin/dhcpcd", "-b", "-n", wpa.interface, NULL};
        char *disconnect[] = {"/sbin/dhcpcd", "-k", wpa.interface, NULL};
        result = er_run_timeout(id ? connect : disconnect, NULL, 0, timeout_ms ? timeout_ms : 3000);
        if (result == -EIO && !id && dhcp_stopped())
            result = 0;
    }
    if (!result)
        result = wait_connection(&wpa, id, address, timeout_ms, out);
    er_wpa_close(&wpa);
    return result;
}

int er_kobo_wifi_connect(const er_device *d, const char *id, bool address, int timeout_ms,
                         struct er_wifi_result *out)
{
    return network_id(id) ? -EINVAL : change_connection(d, id, address, timeout_ms, out);
}

int er_kobo_wifi_disconnect(const er_device *d, int timeout_ms, struct er_wifi_result *out)
{
    return change_connection(d, NULL, false, timeout_ms, out);
}
