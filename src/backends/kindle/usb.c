#define _GNU_SOURCE
#include "usb.h"
#include "core/device.h"
#include "linux/io.h"
#include "linux/usb_configfs.h"
#include <errno.h>
#include <glob.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int volume_state(struct er_kindle_usb *saved, bool capture, bool *paused)
{
    glob_t processes = {0};
    if (glob("/proc/[0-9]*/comm", 0, NULL, &processes))
        return -ENODEV;
    unsigned matches = 0;
    int result = 0;
    for (size_t i = 0; i < processes.gl_pathc; ++i) {
        long pid;
        if (sscanf(processes.gl_pathv[i], "/proc/%ld/comm", &pid) != 1)
            continue;
        char path[64], name[64];
        if (er_read_text(processes.gl_pathv[i], name, sizeof(name)) || strcmp(name, "volumd\n"))
            continue;
        ++matches;
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        char state[1024];
        result = er_read_text(path, state, sizeof(state));
        const char *tail = result ? NULL : strrchr(state, ')');
        if (!tail || strlen(tail) < 3) {
            result = -EIO;
            break;
        }
        *paused = tail[2] == 'T' || tail[2] == 't';
        char *save;
        unsigned field = 0;
        unsigned long long started = 0;
        for (char *word = strtok_r((char *)tail + 2, " ", &save); word;
             word = strtok_r(NULL, " ", &save), ++field)
            if (field == 19)
                started = strtoull(word, NULL, 10);
        if (!started ||
            (!capture && (saved->volume_pid != pid || saved->volume_start != started))) {
            result = -EBUSY;
            break;
        }
        if (capture) {
            saved->volume_pid = (pid_t)pid;
            saved->volume_start = started;
        }
    }
    globfree(&processes);
    return result ? result : matches == 1 ? 0 : -EBUSY;
}

static int volume_paused(struct er_kindle_usb *state)
{
    bool paused = false;
    int result = volume_state(state, false, &paused);
    return result ? result : paused ? 0 : -EBUSY;
}

int er_kindle_usb_service_ready(void)
{
    struct er_kindle_usb state = {0};
    bool paused = false;
    int result = volume_state(&state, true, &paused);
    return result ? result : paused ? -EBUSY : 0;
}

static int pause_volume(struct er_kindle_usb *state)
{
    bool paused = false;
    int result = volume_state(state, false, &paused);
    if (result || paused)
        return result ? result : -EBUSY;
    if (kill(state->volume_pid, SIGSTOP))
        return -errno;
    state->paused_by_hal = true;
    int64_t deadline = er_now_ms() + 2000;
    do {
        result = volume_state(state, false, &paused);
        if (result || paused)
            return result;
        er_sleep_ms(10);
    } while (er_now_ms() < deadline);
    return -ETIMEDOUT;
}

static int resume_volume(struct er_kindle_usb *state)
{
    if (!state->paused_by_hal)
        return 0;
    bool paused = false;
    int result = volume_state(state, false, &paused);
    if (result)
        return result;
    if (paused && kill(state->volume_pid, SIGCONT))
        return -errno;
    state->paused_by_hal = false;
    return 0;
}

static int interface(int request, struct ifreq *value)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;
    snprintf(value->ifr_name, sizeof(value->ifr_name), "usb0");
    int result = ioctl(fd, request, value) ? -errno : 0;
    close(fd);
    return result;
}

static int parameter(struct er_kindle_usb *state, const char *name, const char *value)
{
    if (state->parameter_count == 16)
        return -E2BIG;
    int size = snprintf(state->parameters[state->parameter_count], 256, "%s=%s", name, value);
    if (size < 0 || size >= 256)
        return -E2BIG;
    ++state->parameter_count;
    return 0;
}

static int capture_network(struct er_kindle_usb *state, const struct er_usb_config *config)
{
    int result = volume_paused(state);
    if (result)
        return result;
    struct ifreq value = {0};
    result = interface(SIOCGIFADDR, &value);
    if (result)
        return result;
    state->address = ((const struct sockaddr_in *)&value.ifr_addr)->sin_addr;
    result = interface(SIOCGIFNETMASK, &value);
    if (result)
        return result;
    state->netmask = ((const struct sockaddr_in *)&value.ifr_netmask)->sin_addr;
    result = interface(SIOCGIFFLAGS, &value);
    if (result)
        return result;
    state->flags = value.ifr_flags;
    state->parameter_count = 0;
    state->device_mac[0] = state->host_mac[0] = 0;
    glob_t parameters = {0};
    if (glob("/sys/module/g_ether/parameters/*", 0, NULL, &parameters))
        return -ENODEV;
    for (size_t i = 0; i < parameters.gl_pathc; ++i) {
        const char *name = strrchr(parameters.gl_pathv[i], '/') + 1;
        char text[256];
        result = er_read_text(parameters.gl_pathv[i], text, sizeof(text));
        if (result)
            break;
        text[strcspn(text, "\r\n")] = 0;
        if (!strcmp(name, "dev_addr"))
            snprintf(state->device_mac, sizeof(state->device_mac), "%.31s", text);
        else if (!strcmp(name, "host_addr"))
            snprintf(state->host_mac, sizeof(state->host_mac), "%.31s", text);
        else if (text[0] && strcmp(text, "(null)"))
            result = parameter(state, name, text);
        if (result)
            break;
    }
    globfree(&parameters);
    if (result)
        return result;
    if (!state->device_mac[0] || !strcmp(state->device_mac, "(null)")) {
        result = er_read_text(
            "/sys/class/net/usb0/address", state->device_mac, sizeof(state->device_mac));
        if (result)
            return result;
        state->device_mac[strcspn(state->device_mac, "\r\n")] = 0;
    }
    if ((!state->host_mac[0] || !strcmp(state->host_mac, "(null)")) && config->host_mac)
        snprintf(state->host_mac, sizeof(state->host_mac), "%s", config->host_mac);
    if (!state->device_mac[0] || !strcmp(state->device_mac, "(null)") || !state->host_mac[0] ||
        !strcmp(state->host_mac, "(null)"))
        return -ENODATA;
    result = parameter(state, "dev_addr", state->device_mac);
    return result ? result : parameter(state, "host_addr", state->host_mac);
}

int er_kindle_usb_acquire(struct er_kindle_usb *state, const struct er_usb_config *config)
{
    bool reenumerated = false;
    int result = er_kindle_usb_claim(state, &reenumerated);
    if (result)
        return result;
    bool paused = false;
    result = volume_state(state, true, &paused);
    if (result || paused)
        return result ? result : -EBUSY;
    result = pause_volume(state);
    if (!result)
        result = capture_network(state, config);
    return result;
}

int er_kindle_usb_detach(struct er_kindle_usb *state, const char *controller)
{
    int result = volume_paused(state);
    if (result)
        return result;
    result = er_usb_disconnect_controller(controller);
    if (result)
        return result;
    char *args[] = {"/sbin/rmmod", "g_ether", NULL};
    result = er_run(args, NULL, 0);
    if (!result)
        state->detached = true;
    else {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/udc/%s/soft_connect", controller);
        er_write_text(path, "connect");
    }
    return result;
}

int er_kindle_usb_address(const struct er_kindle_usb *state)
{
    struct ifreq value = {0};
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = state->address};
    memcpy(&value.ifr_addr, &address, sizeof(address));
    int result = interface(SIOCSIFADDR, &value);
    if (!result) {
        address.sin_addr = state->netmask;
        memcpy(&value.ifr_netmask, &address, sizeof(address));
        result = interface(SIOCSIFNETMASK, &value);
    }
    if (!result) {
        value.ifr_flags = state->flags;
        result = interface(SIOCSIFFLAGS, &value);
    }
    return result;
}

int er_kindle_usb_return_network(struct er_kindle_usb *state)
{
    int result = 0;
    if (state->detached) {
        result = volume_paused(state);
        if (result)
            return result;
        if (access("/sys/module/g_ether", F_OK)) {
            char *args[19] = {"/sbin/modprobe", "g_ether"};
            for (unsigned i = 0; i < state->parameter_count; ++i)
                args[i + 2] = state->parameters[i];
            result = er_run(args, NULL, 0);
            if (result)
                return result;
        }
        result = er_kindle_usb_address(state);
        if (result)
            return result;
        state->detached = false;
    }
    return resume_volume(state);
}

int er_kindle_usb_restore(struct er_kindle_usb *state)
{
    int result = er_kindle_usb_return_network(state);
    if (!result && state->policy_saved && state->policy_changed) {
        /* Restore firmware policy without forcing userstore export under the caller.
         * Normal USB connection events decide when stock storage becomes active. */
        result = er_kindle_usb_set_policy(state->original_policy);
        if (!result)
            state->policy_changed = false;
    }
    return result;
}
