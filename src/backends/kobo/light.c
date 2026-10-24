#include "light.h"
#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define KOBO_LIGHT "/sys/class/backlight/mxc_msp430.0/brightness"

struct channel {
    int brightness, power;
};

static int read_channels(const er_device *d, struct channel channels[3])
{
    memset(channels, 0, 3 * sizeof(*channels));
    for (unsigned i = 0; i < 3; ++i) {
        const char *directory = d->profile->light_channels[i];
        if (!directory)
            continue;
        char path[256];
        snprintf(path, sizeof(path), "%s/brightness", directory);
        int result = er_read_int(path, &channels[i].brightness);
        if (!result) {
            snprintf(path, sizeof(path), "%s/bl_power", directory);
            result = er_read_int(path, &channels[i].power);
        }
        if (result)
            return result;
        if (channels[i].brightness < 0 || channels[i].brightness > 255)
            return -EIO;
    }
    return 0;
}

static int write_channel(const char *directory, struct channel value)
{
    if (!directory)
        return 0;
    char path[256];
    snprintf(path, sizeof(path), "%s/bl_power", directory);
    int result = er_write_int(path, value.power);
    if (!result) {
        snprintf(path, sizeof(path), "%s/brightness", directory);
        result = er_write_int(path, value.brightness);
    }
    return result;
}

static int percentage(double value)
{
    return value < 0 ? 0 : value > 100 ? 100 : (int)(value + 0.5);
}

static void channel_levels(const er_device *d, const struct channel channels[3], int *brightness,
                           int *warmth)
{
    int white = channels[0].power ? channels[0].brightness : 0;
    int red = channels[1].power ? channels[1].brightness : 0;
    int green = channels[2].power ? channels[2].brightness : 0;
    if (d->light_valid && white == d->light_values[0] && red == d->light_values[1] &&
        green == d->light_values[2]) {
        *brightness = d->light_brightness;
        *warmth = d->light_warmth;
        return;
    }
    if (!white && !red && !green) {
        *brightness = 0;
        *warmth = d->light_warmth;
        return;
    }
    /* Invert the device's fourth-root LED curves; readback is quantized. */
    double cool = white ? pow((white + 25.5) / 25, 4) : 0;
    double warm = red ? pow((red + 0.5) / 24, 4) : green ? pow((green + 65.5) / 24, 4) : 0;
    *brightness = percentage((cool + warm) / 100);
    if (!*brightness)
        *brightness = 1;
    *warmth = percentage(100 * warm / (cool + warm));
}

int er_kobo_light_probe(const er_device *d, enum er_capability capability)
{
    if (d->profile->light_channels[0]) {
        for (unsigned i = 0; i < 3; ++i) {
            if (!d->profile->light_channels[i])
                continue;
            const char *attributes[] = {"brightness", "bl_power"};
            for (unsigned j = 0; j < 2; ++j) {
                char path[256];
                snprintf(path, sizeof(path), "%s/%s", d->profile->light_channels[i], attributes[j]);
                if (access(path, R_OK | W_OK))
                    return -errno;
            }
        }
        return 0;
    }
    const char *path = capability == ER_WARMTH ? d->warmth_path : KOBO_LIGHT;
    return access(path, R_OK | W_OK) ? -errno : 0;
}

int er_kobo_light_get(const er_device *d, int *brightness, int *warmth)
{
    struct channel channels[3];
    int result = read_channels(d, channels);
    if (!result)
        channel_levels(d, channels, brightness, warmth);
    return result;
}

int er_kobo_light_set(er_device *d, int brightness, int warmth)
{
    struct channel previous[3], next[3];
    int result = read_channels(d, previous);
    if (result)
        return result;
    int old_brightness, old_warmth;
    channel_levels(d, previous, &old_brightness, &old_warmth);
    if (brightness < 0)
        brightness = old_brightness;
    if (warmth < 0)
        warmth = old_warmth;
    double cool = sqrt(sqrt((double)brightness * (100 - warmth)));
    double warm = sqrt(sqrt((double)brightness * warmth));
    double values[] = {25 * cool - 25, 24 * warm, 24 * warm - 65};
    for (unsigned i = 0; i < 3; ++i) {
        int value = values[i] < 0 ? 0 : values[i] > 255 ? 255 : (int)values[i];
        next[i] = (struct channel){value, value ? 31 : 0};
        if (!d->profile->light_channels[i])
            next[i] = (struct channel){0};
    }
    for (unsigned i = 0; i < 3; ++i) {
        result = write_channel(d->profile->light_channels[i], next[i]);
        if (result) {
            d->light_valid = false;
            for (unsigned j = 0; j <= i; ++j)
                write_channel(d->profile->light_channels[j], previous[j]);
            return result;
        }
    }
    d->light_brightness = brightness;
    d->light_warmth = warmth;
    for (unsigned i = 0; i < 3; ++i)
        d->light_values[i] = next[i].brightness;
    d->light_valid = true;
    return 0;
}
