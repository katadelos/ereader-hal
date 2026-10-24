#define _GNU_SOURCE
#include "core/capabilities.h"
#include "backends/kindle/lipc_api.h"
#include "backends/kobo/light.h"
#include "backends/network.h"
#include "controls/power.h"
#include "core/device.h"
#include "linux/framebuffer.h"
#include "linux/io.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *names[ER_CAPABILITY_COUNT] = {
    "brightness",
    "warmth",
    "wifi",
    "battery",
    "display",
    "rtc-wake",
    "suspend",
    "rotation",
    "touch",
    "buttons",
    "cover",
    "orientation",
    "power-events",
    "usb",
    "frontlight",
    "auto-brightness",
    "inhibit-hal",
    "inhibit-system",
    "wifi-status",
    "wifi-scan",
    "wifi-profiles",
    "wifi-connect",
    "awake-lease",
    "battery-details",
};

const char *er_capability_name(enum er_capability capability)
{
    for (unsigned i = 0; i < ER_CAPABILITY_COUNT; ++i)
        if ((uint32_t)capability == (1u << i))
            return names[i];
    return NULL;
}

static const char *reason_names[] = {
    "none",
    "unsupported",
    "not-implemented",
    "permission",
    "driver-missing",
    "service-unavailable",
    "radio-off",
    "resource-busy",
    "timeout",
    "io-error",
    "invalid-configuration",
};

const char *er_capability_reason_name(enum er_capability_reason reason)
{
    if ((unsigned)reason >= sizeof(reason_names) / sizeof(reason_names[0]))
        return NULL;
    return reason_names[reason];
}

enum er_capability_reason er_capability_reason_from_error(int error)
{
    if (error < 0)
        error = -error;
    switch (error) {
    case 0:
        return ER_REASON_NONE;
    case EOPNOTSUPP:
        return ER_REASON_UNSUPPORTED;
    case ENOSYS:
        return ER_REASON_NOT_IMPLEMENTED;
    case EACCES:
    case EPERM:
        return ER_REASON_PERMISSION;
    case ENOENT:
    case ENODEV:
    case ENXIO:
        return ER_REASON_DRIVER_MISSING;
    case ECONNREFUSED:
    case ENOTCONN:
        return ER_REASON_SERVICE_UNAVAILABLE;
    case ENETDOWN:
        return ER_REASON_RADIO_OFF;
    case EBUSY:
    case EAGAIN:
        return ER_REASON_RESOURCE_BUSY;
    case ETIMEDOUT:
        return ER_REASON_TIMEOUT;
    case EINVAL:
    case ERANGE:
        return ER_REASON_INVALID_CONFIGURATION;
    default:
        return ER_REASON_IO_ERROR;
    }
}

void er_capability_finish(struct er_capability_info *info, int result)
{
    info->available = result == 0;
    info->blocked = result == -EBUSY || result == -EAGAIN || result == -ENETDOWN;
    info->error = -result;
    if (!result || info->reason_code == ER_REASON_NONE)
        info->reason_code = er_capability_reason_from_error(result);
    if (!info->reason[0])
        snprintf(info->reason,
                 sizeof(info->reason),
                 "%s",
                 result ? strerror(-result) : "Ready; no known blockers");
}

static int accessible(const char *path, int mode)
{
    return access(path, mode) ? -errno : 0;
}

static int kindle_property(const char *source, const char *property, int *value,
                           struct er_capability_info *info)
{
    int result = er_kindle_get(source, property, value);
    if (result == -ENETDOWN || result == -ENOENT || result == -EOPNOTSUPP) {
        info->reason_code = ER_REASON_SERVICE_UNAVAILABLE;
        snprintf(info->reason, sizeof(info->reason), "%s.%s is unavailable", source, property);
    }
    return result;
}

int er_get_capability(er_device *d, enum er_capability cap, struct er_capability_info *info)
{
    if (!d || !info || !er_capability_name(cap))
        return -EINVAL;
    *info = (struct er_capability_info){0};
    info->supported = (d->info.capabilities & cap) != 0;
    if (!info->supported) {
        info->error = EOPNOTSUPP;
        info->reason_code = ER_REASON_UNSUPPORTED;
        snprintf(info->reason, sizeof(info->reason), "Not supported by this device profile");
        return 0;
    }
    info->implemented = (d->implemented & cap) != 0;
    if (!info->implemented) {
        info->error = ENOSYS;
        info->reason_code = ER_REASON_NOT_IMPLEMENTED;
        const char *reason =
            "Hardware present; HAL binding not implemented for this model/interface";
        if (cap == ER_AUTO_BRIGHTNESS)
            reason = "Ambient-light sensor present; firmware control binding not implemented";
        else if (cap == ER_WIFI)
            reason = "Kobo radio power sequence not implemented for the detected module family";
        else if (cap == ER_ORIENTATION && d->vendor == ER_KINDLE)
            reason = "Kindle orientation event binding not implemented";
        else if (d->profile->display_driver == ER_KOBO_SUNXI)
            reason = "Allwinner requires a buffer-owning display/input backend";
        else if (d->profile->display_driver == ER_DISPLAY_UNKNOWN)
            reason = "Display/input binding is not implemented for this board";
        else if (cap == ER_BRIGHTNESS || cap == ER_WARMTH || cap == ER_FRONTLIGHT)
            reason = "Independent light-channel mixer not implemented";
        snprintf(info->reason, sizeof(info->reason), "%s", reason);
        return 0;
    }
    int result = 0, value;
    char text[128];
    struct er_display display;
    struct er_usb usb;
    switch (cap) {
    case ER_BRIGHTNESS:
    case ER_FRONTLIGHT:
        info->maximum = 100;
        info->steps = d->brightness_max + 1;
        if (d->vendor == ER_KOBO) {
            result = er_kobo_light_probe(d, cap);
        } else {
            result = kindle_property("com.lab126.powerd", "flIntensity", &value, info);
            if (!result)
                result = kindle_property("com.lab126.powerd", "flMaxIntensity", &value, info);
            if (!result && (value <= 0 || value > 10000))
                result = -EIO;
            if (!result) {
                d->brightness_max = value + (d->profile->light_off_path != NULL);
                info->steps = d->brightness_max + 1;
            }
        }
        if (!result && d->profile->light_off_path)
            result = accessible(d->profile->light_off_path, R_OK | W_OK);
        break;
    case ER_WARMTH:
        info->maximum = 100;
        info->steps = d->vendor == ER_KOBO ? (d->profile->light_channels[0] ? 101 : 11) : 25;
        result = d->vendor == ER_KOBO
                     ? er_kobo_light_probe(d, cap)
                     : kindle_property("com.lab126.powerd", "currentAmberLevel", &value, info);
        break;
    case ER_WIFI:
        if (d->vendor == ER_KINDLE)
            result = kindle_property("com.lab126.wifid", "enable", &value, info);
        else
            result = er_kobo_wifi_probe(d, info);
        break;
    case ER_WIFI_STATUS:
    case ER_WIFI_SCAN:
    case ER_WIFI_PROFILES:
    case ER_WIFI_CONNECT:
        result = d->vendor == ER_KINDLE ? er_kindle_network_probe(d, cap, info)
                                        : er_kobo_network_probe(d, cap, info);
        break;
    case ER_AWAKE_LEASE:
        info->minimum = 1;
        info->maximum = 86400;
        info->steps = 86400;
        result = er_awake_probe(d, info->reason, sizeof(info->reason));
        break;
    case ER_BATTERY_DETAILS: {
        struct er_battery_details details;
        result = er_get_battery_details(d, &details);
    } break;
    case ER_BATTERY:
    case ER_POWER_EVENTS: {
        struct er_battery battery;
        result = er_get_battery(d, &battery);
    } break;
    case ER_DISPLAY:
    case ER_ROTATION:
        result = er_read_display(&display);
        if (!result && cap != ER_DISPLAY)
            result = accessible("/dev/fb0", R_OK | W_OK);
        if (cap == ER_ROTATION) {
            info->maximum = 270;
            info->steps = 4;
        }
        break;
    case ER_RTC_WAKE:
        info->minimum = 1;
        info->maximum = 86400;
        info->steps = 86400;
        result = accessible("/sys/class/rtc/rtc0/wakealarm", R_OK | W_OK);
        if (!result)
            result = er_read_text("/sys/class/rtc/rtc0/wakealarm", text, sizeof(text));
        if (!result && text[0] && text[0] != '\n') {
            result = -EBUSY;
            snprintf(info->reason, sizeof(info->reason), "An RTC alarm is already armed");
        }
        break;
    case ER_SUSPEND:
        if (!strcmp(d->info.suspend_mode, "none"))
            result = -ENODEV;
        else
            result = accessible("/sys/power/state", W_OK);
        if (!result)
            result = er_suspend_blocker(d, info->reason, sizeof(info->reason));
        break;
    case ER_TOUCH:
    case ER_BUTTONS:
    case ER_COVER:
    case ER_ORIENTATION:
        if (cap == ER_COVER && d->vendor == ER_KINDLE) {
            result = er_read_int("/sys/devices/platform/eink_hall/hall_detect", &value);
        } else {
            result = er_input_capability_probe(d, cap);
        }
        break;
    case ER_USB:
        result = er_get_usb(d, &usb);
        break;
    case ER_INHIBIT_HAL:
        result = accessible("/var/run", W_OK);
        break;
    case ER_INHIBIT_SYSTEM:
        result = accessible("/sys/power/wake_lock", W_OK);
        if (!result)
            result = accessible("/sys/power/wake_unlock", W_OK);
        if (!result)
            result = accessible("/var/run", W_OK);
        break;
    default:
        result = -EOPNOTSUPP;
        break;
    }
    er_capability_finish(info, result);
    return 0;
}
