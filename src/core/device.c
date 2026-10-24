#define _GNU_SOURCE
#include "core/device.h"
#include "backends/kobo/setup.h"
#include "linux/framebuffer.h"
#include "linux/io.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void configure_device(er_device *d)
{
    const struct er_profile *p = d->profile;
    d->vendor = p->vendor;
    er_profile_info(p, &d->info);
    d->brightness_max = p->vendor == ER_KOBO ? 100 : 24;
    d->implemented = d->info.capabilities;
    if (p->battery_capacity)
        snprintf(d->battery_capacity, sizeof(d->battery_capacity), "%s", p->battery_capacity);
    if (p->battery_status)
        snprintf(d->battery_status, sizeof(d->battery_status), "%s", p->battery_status);
    if (p->warmth_path)
        snprintf(d->warmth_path, sizeof(d->warmth_path), "%s", p->warmth_path);
    if (p->vendor == ER_KOBO)
        er_kobo_configure(d);
    if (!p->light_off)
        d->brightness_max = 25;
    d->implemented &= ~ER_AUTO_BRIGHTNESS;
    if (p->vendor == ER_KINDLE)
        d->implemented &= ~ER_ORIENTATION;
    if (p->display_driver == ER_KOBO_SUNXI || p->display_driver == ER_DISPLAY_UNKNOWN)
        d->implemented &= ~(ER_ROTATION | ER_TOUCH | ER_ORIENTATION);
    if (p->display_driver == ER_EINKFB)
        d->implemented &= ~ER_ROTATION;
    struct er_display display;
    if (!er_read_display(&display)) {
        d->info.panel_width = display.width < display.height ? display.width : display.height;
        d->info.panel_height = display.width > display.height ? display.width : display.height;
    }
}

int er_open(er_device **out)
{
    if (!out)
        return -EINVAL;
    *out = NULL;
    const struct er_profile *profile = er_detect_profile();
    if (!profile)
        return -ENODEV;
    er_device *d = calloc(1, sizeof(*d));
    if (!d)
        return -ENOMEM;
    d->profile = profile;
    configure_device(d);
    d->saved_brightness = -1;
    d->inhibit_fd = -1;
    d->low_battery_threshold = 10;
    for (int i = 0; i < ER_INPUT_COUNT; ++i)
        d->input[i].fd = -1;
    char states[128];
    d->info.suspend_mode = "none";
    if (!er_read_text("/sys/power/state", states, sizeof(states))) {
        char *save;
        for (char *word = strtok_r(states, " \n", &save); word;
             word = strtok_r(NULL, " \n", &save)) {
            if (!strcmp(word, "mem")) {
                d->info.suspend_mode = "mem";
                break;
            }
            if (!strcmp(word, "freeze"))
                d->info.suspend_mode = "freeze";
        }
    }
    er_input_open(d);
    struct er_display display;
    if (!er_get_display(d, &display))
        d->input_rotation = display.rotation;
    *out = d;
    return 0;
}

int er_close(er_device *d)
{
    if (!d)
        return 0;
    int result = er_release_usb(d);
    if (!result)
        result = er_release_awake(d);
    while (!result && d->inhibit_count)
        result = er_release_suspend(d);
    if (result)
        return result;
    er_input_close(d);
    free(d->usb_config);
    free(d);
    return 0;
}

const struct er_info *er_get_info(const er_device *d)
{
    return d ? &d->info : NULL;
}

int er_supported(const er_device *d, enum er_capability cap)
{
    if (!d)
        return -EINVAL;
    if (!(d->info.capabilities & cap))
        return -EOPNOTSUPP;
    return (d->implemented & cap) ? 0 : -ENOSYS;
}
