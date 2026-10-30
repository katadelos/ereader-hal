#define _GNU_SOURCE
#include "backends/network.h"
#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define DVFS_CONTROL "/sys/devices/platform/mxc_dvfs_core.0/enable"
#define CPU_GOVERNOR "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define CPU_SETSPEED "/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed"

static int interface_flags(const er_device *d, bool set, bool *enabled)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;
    struct ifreq req = {0};
    snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", d->wifi_interface);
    int result = ioctl(fd, SIOCGIFFLAGS, &req) < 0 ? -errno : 0;
    if (!result && set) {
        if (*enabled)
            req.ifr_flags |= IFF_UP;
        else
            req.ifr_flags &= ~IFF_UP;
        if (ioctl(fd, SIOCSIFFLAGS, &req) < 0)
            result = -errno;
    } else if (!result) {
        *enabled = (req.ifr_flags & IFF_UP) != 0;
    }
    close(fd);
    return result;
}

int er_kobo_wifi_get(const er_device *d, bool *enabled)
{
    int result = interface_flags(d, false, enabled);
    if (result == -ENODEV) {
        *enabled = false;
        return 0;
    }
    return result;
}

static bool module_loaded(const char *name)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/module/%s", name);
    return name[0] && access(path, F_OK) == 0;
}

static int dvfs_snapshot(struct er_kobo_wifi_suspend_state *state)
{
    if (access(DVFS_CONTROL, F_OK))
        return errno == ENOENT ? 0 : -errno;
    char text[64];
    int result = er_read_line(DVFS_CONTROL, text, sizeof(text));
    if (result)
        return result;
    if (!strcmp(text, "DVFS is enabled"))
        state->dvfs_enabled = true;
    else if (strcmp(text, "DVFS is disabled"))
        return -EPROTO;
    result = er_read_line(CPU_GOVERNOR, state->governor, sizeof(state->governor));
    if (!result && !strcmp(state->governor, "userspace")) {
        result = er_read_int(CPU_SETSPEED, &state->frequency);
        if (!result && state->frequency <= 0)
            result = -EPROTO;
    }
    state->dvfs_saved = !result;
    return result;
}

static int dvfs_restore(const struct er_kobo_wifi_suspend_state *state)
{
    if (!state->dvfs_saved)
        return 0;
    /* The stock driver ignores cpufreq targets while hardware DVFS is active.
     * Restore policy after radio recovery, even if a module operation failed. */
    int result = er_write_text(DVFS_CONTROL, "0");
    int next = er_write_text(CPU_GOVERNOR, state->governor);
    if (!result)
        result = next;
    if (!strcmp(state->governor, "userspace")) {
        next = er_write_int(CPU_SETSPEED, state->frequency);
        if (!result)
            result = next;
    }
    if (state->dvfs_enabled) {
        next = er_write_text(DVFS_CONTROL, "1");
        if (!result)
            result = next;
    }
    return result;
}

static int suspend_state(const er_device *d, struct er_kobo_wifi_suspend_state *state)
{
    *state = (struct er_kobo_wifi_suspend_state){0};
    int result = er_kobo_wifi_get(d, &state->enabled);
    if (result)
        return result;
    state->radio_loaded = module_loaded(d->wifi_module);
    state->power_loaded = d->wifi_power == ER_WIFI_POWER_MODULE && module_loaded("sdio_wifi_pwr");
    state->dependency_loaded = !strcmp(d->wifi_module, "moal") && module_loaded("mlan");
    /* WMT keeps its shared connectivity modules loaded when Wi-Fi is off.
     * Other backends remove their radio driver as part of powering off. */
    state->changed = state->enabled || (d->wifi_power != ER_WIFI_POWER_WMT &&
                                        (state->radio_loaded || state->power_loaded));
    return state->changed && d->wifi_power == ER_WIFI_POWER_MODULE ? dvfs_snapshot(state) : 0;
}

static int reloadable(const char *path)
{
    return !path[0] ? -ENOENT : access(path, R_OK) ? -errno : 0;
}

int er_kobo_wifi_suspend_probe(const er_device *d, char *reason, size_t size)
{
    struct er_kobo_wifi_suspend_state state;
    int result = suspend_state(d, &state);
    if (result)
        goto failed;
    if (!state.changed)
        return 0;
    if (!d->wifi_module[0]) {
        result = -ENOTSUP;
        goto failed;
    }
    if (state.enabled && !state.radio_loaded) {
        result = -EPROTO;
        goto failed;
    }
    /* A netdev parent or device-tree flag cannot prove a stock SDIO driver's
     * suspend contract. dhd exposes a virtual netdev and must be detached
     * before the host removes its card. Quiesce all managed radios through
     * the backend which already owns their module and power lifecycle. */
    if (d->wifi_power != ER_WIFI_POWER_WMT && state.radio_loaded)
        result = reloadable(d->wifi_path);
    if (!result && state.power_loaded)
        result = reloadable(d->wifi_power_path);
    if (!result && state.dependency_loaded)
        result = reloadable(d->wifi_dependency_path);
    if (!result && state.radio_loaded && d->wifi_power == ER_WIFI_POWER_MODULE &&
        !state.power_loaded)
        result = -EPROTO;
    if (!result && state.radio_loaded && !strcmp(d->wifi_module, "moal") &&
        !state.dependency_loaded)
        result = -EPROTO;
    if (!result && (state.enabled || state.radio_loaded)) {
        struct er_capability_info info = {0};
        result = er_kobo_wifi_probe(d, &info);
    }
failed:
    if (result)
        snprintf(reason, size, "Cannot preserve Wi-Fi across suspend: %s", strerror(-result));
    return result;
}

int er_kobo_wifi_suspend_prepare(const er_device *d, struct er_kobo_wifi_suspend_state *state)
{
    *state = (struct er_kobo_wifi_suspend_state){0};
    char reason[128];
    int result = er_kobo_wifi_suspend_probe(d, reason, sizeof(reason));
    if (result)
        return result;
    result = suspend_state(d, state);
    if (result) {
        *state = (struct er_kobo_wifi_suspend_state){0};
        return result;
    }
    if (!state->changed)
        return 0;
    /* Retain the snapshot on partial failure for the common cleanup path. */
    result = er_kobo_wifi_set(d, false);
    if (!result && d->wifi_power != ER_WIFI_POWER_WMT && module_loaded(d->wifi_module))
        result = -EBUSY;
    if (!result && state->power_loaded && module_loaded("sdio_wifi_pwr"))
        result = -EBUSY;
    bool enabled;
    if (!result)
        result = er_kobo_wifi_get(d, &enabled);
    return !result && enabled ? -EBUSY : result;
}

static int module(bool load, const char *name, const char *path)
{
    char *insert[] = {"/sbin/insmod", (char *)path, NULL};
    char *remove[] = {"/sbin/rmmod", (char *)name, NULL};
    int result = er_run(load ? insert : remove, NULL, 0);
    if (!result)
        result = er_sleep_ms(250);
    return result;
}

static void sibling_module(const er_device *d, const char *name, char path[256])
{
    const char *slash = strrchr(d->wifi_path, '/');
    snprintf(path, 256, "%.*s/%s.ko", slash ? (int)(slash - d->wifi_path) : 0, d->wifi_path, name);
}

static const char *const wmt_modules[] = {"wmt_drv", "wmt_chrdev_wifi", "wmt_cdev_bt"};

static const char *dhd_tool(void)
{
    static const char *const paths[] = {
        "/bin/wlarm_le", "/sbin/wlarm_le", "/usr/bin/wlarm_le", "/usr/sbin/wlarm_le"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
        if (!access(paths[i], X_OK))
            return paths[i];
    return NULL;
}

static int module_available(const char *name, const char *path)
{
    if (module_loaded(name))
        return 0;
    return !path[0] ? -ENOENT : access(path, R_OK) ? -errno : 0;
}

int er_kobo_wifi_probe(const er_device *d, struct er_capability_info *info)
{
    const char *required = "Wi-Fi driver module";
    int result = module_available(d->wifi_module, d->wifi_path);
    if (!result && !strcmp(d->wifi_module, "moal")) {
        required = "Wi-Fi dependency module";
        result = module_available("mlan", d->wifi_dependency_path);
    }
    if (!result && d->wifi_power == ER_WIFI_POWER_MODULE) {
        required = "Wi-Fi power module";
        result = module_available("sdio_wifi_pwr", d->wifi_power_path);
    }
    if (!result && d->wifi_power == ER_WIFI_POWER_WMT) {
        required = "MediaTek connectivity module";
        for (size_t i = 0; !result && i < sizeof(wmt_modules) / sizeof(wmt_modules[0]); ++i) {
            char path[256];
            sibling_module(d, wmt_modules[i], path);
            result = module_available(wmt_modules[i], path);
        }
    }
    if (result) {
        info->reason_code =
            result == -EACCES || result == -EPERM ? ER_REASON_PERMISSION : ER_REASON_DRIVER_MISSING;
        snprintf(info->reason,
                 sizeof(info->reason),
                 "%s is unavailable: %s",
                 required,
                 strerror(-result));
        return result;
    }
    if (d->wifi_power == ER_WIFI_POWER_NTX && access("/dev/ntx_io", R_OK))
        return -errno;
    if (!strcmp(d->wifi_module, "dhd") && !dhd_tool()) {
        info->reason_code = ER_REASON_SERVICE_UNAVAILABLE;
        snprintf(info->reason,
                 sizeof(info->reason),
                 "Broadcom radio control program wlarm_le is unavailable");
        return -ENOENT;
    }
    return 0;
}

static int ntx_power(bool enabled)
{
    int fd = open("/dev/ntx_io", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    int result = ioctl(fd, 208, (int)enabled) < 0 ? -errno : 0;
    close(fd);
    return result;
}

static int wmt_power(const er_device *d, bool enabled)
{
    if (!enabled)
        return er_write_text("/dev/wmtWifi", "0");
    for (size_t i = 0; i < sizeof(wmt_modules) / sizeof(wmt_modules[0]); ++i) {
        if (module_loaded(wmt_modules[i]))
            continue;
        char path[256];
        sibling_module(d, wmt_modules[i], path);
        int result = module(true, wmt_modules[i], path);
        if (result)
            return result;
    }
    int result = 0;
    if (!module_loaded(d->wifi_module))
        result = module(true, d->wifi_module, d->wifi_path);
    if (!result)
        result = er_write_text("/proc/driver/wmt_dbg", "0xDB9DB9");
    if (!result)
        result = er_write_text("/proc/driver/wmt_dbg", "7 9 0");
    if (!result)
        result = er_sleep_ms(1000);
    if (!result)
        result = er_write_text("/proc/driver/wmt_dbg", "0xDB9DB9");
    if (!result)
        result = er_write_text("/proc/driver/wmt_dbg", "7 9 1");
    return result ? result : er_write_text("/dev/wmtWifi", "1");
}

static int dvfs(bool enabled)
{
    const char *control = DVFS_CONTROL;
    const char *governor = CPU_GOVERNOR;
    if (access(control, F_OK))
        return 0;
    if (enabled) {
        int result = er_write_text(governor, "userspace");
        return result ? result : er_write_text(control, "1");
    }
    int result = er_write_text(control, "0");
    char available[256];
    if (!result)
        result = er_read_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors",
                              available,
                              sizeof(available));
    if (!result && strstr(available, "conservative"))
        return er_write_text(governor, "conservative");
    if (!result)
        result = er_write_text(governor, "userspace");
    char speed[32];
    if (!result)
        result = er_read_line(
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", speed, sizeof(speed));
    return result ? result : er_write_text(CPU_SETSPEED, speed);
}

static int power(const er_device *d, bool enabled)
{
    if (d->wifi_power == ER_WIFI_POWER_NTX)
        return ntx_power(enabled);
    if (d->wifi_power == ER_WIFI_POWER_WMT)
        return wmt_power(d, enabled);
    int result = dvfs(enabled);
    if (!result)
        result = module(enabled, "sdio_wifi_pwr", d->wifi_power_path);
    if (result && enabled)
        dvfs(false);
    return result;
}

static void country_parameter(const er_device *d, char value[32])
{
    value[0] = 0;
    const char *name = !strcmp(d->wifi_module, "8821cs") ? "rtw_country_code"
                       : !strcmp(d->wifi_module, "moal") ? "reg_alpha2"
                                                         : NULL;
    if (!name)
        return;
    FILE *file = fopen("/mnt/onboard/.kobo/Kobo/Kobo eReader.conf", "r");
    if (!file)
        return;
    char line[512], country[3];
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "WifiRegulatoryDomain=%2[A-Z]", country) == 1 && strlen(country) == 2) {
            snprintf(value, 32, "%s=%s", name, country);
            break;
        }
    }
    fclose(file);
}

static int radio_load(const er_device *d)
{
    char country[32];
    country_parameter(d, country);
    char *argv[] = {"/sbin/insmod", (char *)d->wifi_path, NULL, NULL, NULL};
    unsigned count = 2;
    if (!strcmp(d->wifi_module, "moal"))
        argv[count++] = "mod_para=nxp/wifi_mod_para_sd8987.conf";
    if (country[0])
        argv[count++] = country;
    return er_run(argv, NULL, 0);
}

static int dhd_control(const er_device *d, bool enabled)
{
    if (strcmp(d->wifi_module, "dhd"))
        return 0;
    const char *tool = dhd_tool();
    if (!tool)
        return -ENOENT;
    char *argv[] = {(char *)tool, "-i", (char *)d->wifi_interface, enabled ? "up" : "down", NULL};
    return er_run(argv, NULL, 0);
}

static int set_interface(const er_device *d, bool enabled)
{
    int result = 0;
    for (int i = 0; i < 20; ++i) {
        result = interface_flags(d, true, &enabled);
        if (result != -ENODEV)
            break;
        er_sleep_ms(100);
    }
    return result;
}

int er_kobo_wifi_set(const er_device *d, bool enabled)
{
    bool current;
    int result = er_kobo_wifi_get(d, &current);
    if (result || (current && enabled))
        return result;
    struct er_capability_info info = {0};
    if (enabled && (result = er_kobo_wifi_probe(d, &info)))
        return result;
    bool had_power = d->wifi_power == ER_WIFI_POWER_MODULE && module_loaded("sdio_wifi_pwr");
    bool had_wifi = module_loaded(d->wifi_module);
    bool had_dependency = module_loaded("mlan");
    if (!enabled) {
        result = had_wifi ? dhd_control(d, false) : 0;
        if (!result)
            result = interface_flags(d, true, &enabled);
        if (result && result != -ENODEV)
            return result;
        if (d->wifi_power != ER_WIFI_POWER_WMT) {
            if (had_wifi && (result = module(false, d->wifi_module, NULL)))
                return result;
            if (!strcmp(d->wifi_module, "moal") && had_dependency &&
                (result = module(false, "mlan", NULL)))
                return result;
        }
        return d->wifi_power != ER_WIFI_POWER_MODULE || had_power ? power(d, false) : 0;
    }
    if (!had_power) {
        result = power(d, true);
        if (result)
            return result;
        er_sleep_ms(250);
    }
    bool dependency_added = false;
    if (!strcmp(d->wifi_module, "moal") && !had_dependency) {
        result = module(true, "mlan", d->wifi_dependency_path);
        if (result)
            goto undo_power;
        dependency_added = true;
    }
    if (!had_wifi && d->wifi_power != ER_WIFI_POWER_WMT) {
        result = radio_load(d);
        if (result)
            goto undo_dependency;
    }
    er_sleep_ms(!strcmp(d->wifi_module, "moal") ? 2000 : 1000);
    result = set_interface(d, enabled);
    if (!result)
        result = dhd_control(d, true);
    if (!result)
        return 0;
    if (!had_wifi && d->wifi_power != ER_WIFI_POWER_WMT)
        module(false, d->wifi_module, NULL);
undo_dependency:
    if (dependency_added)
        module(false, "mlan", NULL);
undo_power:
    if (!had_power)
        power(d, false);
    return result;
}

int er_kobo_wifi_suspend_restore(const er_device *d, const struct er_kobo_wifi_suspend_state *state)
{
    if (!state->changed)
        return 0;
    /* Recreate the captured resources without the public enable operation's
     * failure unwind: these modules were owned before this sleep attempt. */
    int result = 0, policy;
    bool restore_power = d->wifi_power == ER_WIFI_POWER_MODULE
                             ? state->power_loaded && !module_loaded("sdio_wifi_pwr")
                             : state->enabled || state->radio_loaded;
    if (restore_power) {
        result = power(d, true);
        if (result)
            goto done;
        er_sleep_ms(250);
    }
    if (state->dependency_loaded && !module_loaded("mlan")) {
        result = module(true, "mlan", d->wifi_dependency_path);
        if (result)
            goto done;
    }
    if (!state->radio_loaded)
        goto done;
    bool load_radio = !module_loaded(d->wifi_module);
    if (load_radio) {
        result = radio_load(d);
        if (result)
            goto done;
    }
    if (load_radio || d->wifi_power == ER_WIFI_POWER_WMT)
        er_sleep_ms(!strcmp(d->wifi_module, "moal") ? 2000 : 1000);
    if (state->enabled) {
        result = set_interface(d, true);
        if (!result)
            result = dhd_control(d, true);
    } else {
        result = dhd_control(d, false);
        int restored = set_interface(d, false);
        if (!result)
            result = restored;
    }
done:
    policy = dvfs_restore(state);
    return result ? result : policy;
}
