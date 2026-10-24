#define _GNU_SOURCE
#include "setup.h"
#include "core/device.h"
#include "linux/io.h"
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void unique_path(const char *pattern, char *out, size_t size)
{
    out[0] = 0;
    glob_t paths = {0};
    if (!glob(pattern, 0, NULL, &paths) && paths.gl_pathc == 1)
        snprintf(out, size, "%s", paths.gl_pathv[0]);
    globfree(&paths);
}

int er_kobo_hardware_value(const char *codes, const char *field, char *value, size_t size)
{
    char output[512];
    char *legacy[] = {"/bin/ntx_hwconfig", "-s", "-p", "/dev/mmcblk0", (char *)field, NULL};
    char *mtk[] = {"/bin/ntx_hwconfig", "-S", "1", "-p", "/dev/mmcblk0p6", (char *)field, NULL};
    int result = er_run(er_profile_matches(codes, "389") ? mtk : legacy, output, sizeof(output));
    value[0] = 0;
    if (!result) {
        char *save;
        for (char *line = strtok_r(output, "\r\n", &save); line;
             line = strtok_r(NULL, "\r\n", &save))
            snprintf(value, size, "%s", line);
    }
    return result;
}

static void module_path(const char *platform, const char *name, char *path, size_t size)
{
    const char *directories[] = {"wifi", "", "mt66xx"};
    path[0] = 0;
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        char candidate[256];
        snprintf(candidate,
                 sizeof(candidate),
                 "/drivers/%s/%s%s%s.ko",
                 platform,
                 directories[i],
                 directories[i][0] ? "/" : "",
                 name);
        if (!access(candidate, F_OK)) {
            snprintf(path, size, "%s", candidate);
            return;
        }
    }
}

static void configure_wifi(er_device *d)
{
    static const struct {
        const char *hardware, *module, *interface;
        enum er_wifi_power power;
    } radios[] = {
        {"WC121A2", "dhd", "eth0", ER_WIFI_POWER_MODULE},
        {"CYW43455", "bcmdhd", "wlan0", ER_WIFI_POWER_MODULE},
        {"RTL8192", "8192es", "eth0", ER_WIFI_POWER_MODULE},
        {"RTL8189", "8189fs", "eth0", ER_WIFI_POWER_MODULE},
        {"RTL8723DS", "8723ds", "wlan0", ER_WIFI_POWER_MODULE},
        {"RTL8821CS", "8821cs", "wlan0", ER_WIFI_POWER_MODULE},
        {"88W8987", "moal", "mlan0", ER_WIFI_POWER_NTX},
        {"MT8113T", "wlan_drv_gen4m", "wlan0", ER_WIFI_POWER_WMT},
    };
    snprintf(d->wifi_interface, sizeof(d->wifi_interface), "eth0");
    char hardware[64], cpu[64], platform[80];
    er_kobo_hardware_value(d->profile->codes, "Wifi", hardware, sizeof(hardware));
    er_kobo_hardware_value(d->profile->codes, "CPU", cpu, sizeof(cpu));
    bool freescale = er_profile_matches(d->profile->codes, "310") && !strcmp(cpu, "mx35");
    snprintf(
        platform, sizeof(platform), "%s%s", freescale ? "freescale" : cpu, freescale ? "" : "-ntx");
    for (size_t i = 0; i < sizeof(radios) / sizeof(radios[0]); ++i) {
        if (strcmp(hardware, radios[i].hardware))
            continue;
        snprintf(d->wifi_module, sizeof(d->wifi_module), "%s", radios[i].module);
        snprintf(d->wifi_interface, sizeof(d->wifi_interface), "%s", radios[i].interface);
        d->wifi_power = radios[i].power;
    }
    if (freescale) {
        d->wifi_power = ER_WIFI_POWER_NTX;
        snprintf(d->wifi_module, sizeof(d->wifi_module), "ar6000");
        snprintf(d->wifi_interface, sizeof(d->wifi_interface), "wlan0");
    }
    if (!strcmp(cpu, "b300"))
        d->wifi_power = ER_WIFI_POWER_NTX;
    if (!d->wifi_module[0]) {
        d->implemented &= ~ER_WIFI;
        return;
    }
    module_path(platform, d->wifi_module, d->wifi_path, sizeof(d->wifi_path));
    if (d->wifi_power == ER_WIFI_POWER_MODULE) {
        module_path(platform, "sdio_wifi_pwr", d->wifi_power_path, sizeof(d->wifi_power_path));
    }
    if (!strcmp(d->wifi_module, "moal"))
        module_path(platform, "mlan", d->wifi_dependency_path, sizeof(d->wifi_dependency_path));
}

void er_kobo_configure(er_device *d)
{
    const struct er_profile *p = d->profile;
    /* These boards have multiple PMIC revisions under the same product ID. */
    if (er_profile_matches(p->codes, "388") || er_profile_matches(p->codes, "382") ||
        er_profile_matches(p->codes, "383")) {
        unique_path(
            "/sys/class/power_supply/*/capacity", d->battery_capacity, sizeof(d->battery_capacity));
        size_t length = strlen(d->battery_capacity);
        if (length > 8)
            snprintf(d->battery_status,
                     sizeof(d->battery_status),
                     "%.*sstatus",
                     (int)length - 8,
                     d->battery_capacity);
        if (er_profile_matches(p->codes, "388")) {
            unique_path("/sys/class/backlight/*/color", d->warmth_path, sizeof(d->warmth_path));
            if (!d->warmth_path[0])
                unique_path("/sys/class/leds/*/color", d->warmth_path, sizeof(d->warmth_path));
        }
    }
    /* Independent light channels need a mixer. */
    if ((p->capabilities & ER_WARMTH) && !p->warmth_path && !p->light_channels[0] &&
        !er_profile_matches(p->codes, "388"))
        d->implemented &= ~(ER_WARMTH | ER_BRIGHTNESS | ER_FRONTLIGHT);

    configure_wifi(d);
}
