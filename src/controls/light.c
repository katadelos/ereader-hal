#include "backends/kobo/light.h"
#include "backends/kindle/lipc_api.h"
#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <stdio.h>
#define KOBO_LIGHT "/sys/class/backlight/mxc_msp430.0/brightness"
#define POWERD "com.lab126.powerd"

int er_set_brightness_provider(er_device *d, er_brightness_reader reader,
                               er_brightness_writer writer, void *context)
{
    if (!d || (!reader != !writer))
        return -EINVAL;
    if (reader && !(d->info.capabilities & ER_BRIGHTNESS))
        return -EOPNOTSUPP;
    d->brightness_reader = reader;
    d->brightness_writer = writer;
    d->brightness_context = context;
    d->capability_valid &= ~(ER_BRIGHTNESS | ER_FRONTLIGHT);
    return 0;
}

int er_get_brightness(const er_device *d, int *percent)
{
    if (!percent)
        return -EINVAL;
    int result = er_supported(d, ER_BRIGHTNESS), raw;
    if (result)
        return result;
    if (d->brightness_reader) {
        result = d->brightness_reader(&raw, d->brightness_context);
        if (result)
            return result < 0 ? result : -EIO;
        if (raw < 0 || raw > 100)
            return -ERANGE;
        *percent = raw;
        return 0;
    }
    if (d->profile->light_channels[0]) {
        int warmth;
        return er_kobo_light_get(d, percent, &warmth);
    }
    result = d->vendor == ER_KOBO ? er_read_int(KOBO_LIGHT, &raw)
                                  : er_kindle_get(POWERD, "flIntensity", &raw);
    if (result)
        return result;
    if (d->profile->light_off_path) {
        int physical;
        if (d->profile->display_driver == ER_KINDLE_PEARL) {
            char text[64];
            result = er_read_text(d->profile->light_off_path, text, sizeof(text));
            if (!result && sscanf(text, "FrontLight(Intensity) = %d", &physical) != 1)
                result = -EIO;
        } else {
            result = er_read_int(d->profile->light_off_path, &physical);
        }
        if (result)
            return result;
        raw = physical == 0 ? 0 : raw + 1;
    }
    if (raw < 0 || raw > d->brightness_max)
        return -EIO;
    *percent = (raw * 100 + d->brightness_max / 2) / d->brightness_max;
    return 0;
}

int er_set_brightness(er_device *d, int percent)
{
    if (percent < 0 || percent > 100)
        return -EINVAL;
    int result = er_supported(d, ER_BRIGHTNESS);
    if (result)
        return result;
    if (d->brightness_writer) {
        result = d->brightness_writer(percent, d->brightness_context);
        if (result)
            return result < 0 ? result : -EIO;
        if (percent > 0)
            d->saved_brightness = percent;
        return 0;
    }
    if (d->profile->light_channels[0]) {
        result = er_kobo_light_set(d, percent, -1);
        if (!result && percent > 0)
            d->saved_brightness = percent;
        return result;
    }
    int raw = (percent * d->brightness_max + 50) / 100;
    int native = d->profile->light_off_path && raw > 0 ? raw - 1 : raw;
    result = d->vendor == ER_KOBO ? er_write_int(KOBO_LIGHT, native)
                                  : er_kindle_set(POWERD, "flIntensity", native);
    if (!result && d->profile->light_off_path && raw == 0)
        result = er_write_int(d->profile->light_off_path, 0);
    if (!result && percent > 0)
        d->saved_brightness = percent;
    return result;
}

int er_get_warmth(const er_device *d, int *percent)
{
    if (!percent)
        return -EINVAL;
    int result = er_supported(d, ER_WARMTH), raw;
    if (result)
        return result;
    if (d->profile->light_channels[0]) {
        int brightness;
        return er_kobo_light_get(d, &brightness, percent);
    }
    result = d->vendor == ER_KOBO ? er_read_int(d->warmth_path, &raw)
                                  : er_kindle_get(POWERD, "currentAmberLevel", &raw);
    if (result)
        return result;
    int maximum = d->vendor == ER_KOBO ? 10 : 24;
    if (raw < 0 || raw > maximum)
        return -EIO;
    if (d->vendor == ER_KOBO && d->profile->warmth_inverted)
        raw = maximum - raw;
    *percent = (raw * 100 + maximum / 2) / maximum;
    return 0;
}

int er_set_warmth(er_device *d, int percent)
{
    if (percent < 0 || percent > 100)
        return -EINVAL;
    int result = er_supported(d, ER_WARMTH);
    if (result)
        return result;
    if (d->profile->light_channels[0])
        return er_kobo_light_set(d, -1, percent);
    int maximum = d->vendor == ER_KOBO ? 10 : 24;
    int raw = (percent * maximum + 50) / 100;
    if (d->vendor == ER_KOBO && d->profile->warmth_inverted)
        raw = maximum - raw;
    return d->vendor == ER_KOBO ? er_write_int(d->warmth_path, raw)
                                : er_kindle_set(POWERD, "currentAmberLevel", raw);
}

int er_get_frontlight(const er_device *d, struct er_frontlight *light)
{
    if (!d || !light)
        return -EINVAL;
    int support = er_supported(d, ER_FRONTLIGHT);
    if (support)
        return support;
    struct er_frontlight value = {.warmth = -1, .restore_brightness = d->saved_brightness};
    int result = er_get_brightness(d, &value.brightness);
    if (result)
        return result;
    value.enabled = value.brightness > 0;
    if (d->info.capabilities & ER_WARMTH) {
        result = er_get_warmth(d, &value.warmth);
        if (result)
            return result;
    }
    *light = value;
    return 0;
}

int er_set_frontlight(er_device *d, bool enabled)
{
    int percent;
    int result = er_supported(d, ER_FRONTLIGHT);
    if (result)
        return result;
    result = er_get_brightness(d, &percent);
    if (result)
        return result;
    if (percent > 0)
        d->saved_brightness = percent;
    if (enabled) {
        if (percent > 0)
            return 0;
        if (d->saved_brightness <= 0)
            return -ENODATA;
        return er_set_brightness(d, d->saved_brightness);
    }
    return er_set_brightness(d, 0);
}

int er_set_auto_brightness(const er_device *d, bool enabled)
{
    (void)enabled;
    int result = er_supported(d, ER_AUTO_BRIGHTNESS);
    return result ? result : -ENOSYS;
}
