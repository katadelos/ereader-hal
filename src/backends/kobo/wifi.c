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
    return access(path, F_OK) == 0;
}

int er_kobo_wifi_suspend_blocker(const er_device *d, char *reason, size_t size)
{
    if (!er_profile_matches(d->profile->codes, "377") || strcmp(d->wifi_module, "8192es") ||
        !module_loaded("8192es"))
        return 0;
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s/device/../../../../of_node", d->wifi_interface);
    if (access(path, F_OK))
        return 0;
    strncat(path, "/keep-power-in-suspend", sizeof(path) - strlen(path) - 1);
    if (!access(path, F_OK))
        return 0;
    if (errno != ENOENT)
        return -errno;
    snprintf(
        reason, size, "Turn Wi-Fi off before suspend; its SDIO host cannot keep the radio powered");
    return -EBUSY;
}

int er_kobo_wifi_suspend_prepare(const er_device *d, bool *restore)
{
    *restore = false;
    char reason[128];
    int result = er_kobo_wifi_suspend_blocker(d, reason, sizeof(reason));
    if (result != -EBUSY)
        return result;
    result = er_kobo_wifi_get(d, restore);
    return result ? result : er_kobo_wifi_set(d, false);
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
    const char *control = "/sys/devices/platform/mxc_dvfs_core.0/enable";
    const char *governor = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
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
    return result ? result
                  : er_write_text("/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed", speed);
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
        result = dhd_control(d, false);
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
    for (int i = 0; i < 20; ++i) {
        result = interface_flags(d, true, &enabled);
        if (result != -ENODEV)
            break;
        er_sleep_ms(100);
    }
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
