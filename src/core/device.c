#define _GNU_SOURCE
#include "core/device.h"
#include "backends/kobo/setup.h"
#include "linux/framebuffer.h"
#include "linux/io.h"
#include <ctype.h>
#include <errno.h>
#include <glob.h>
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
    d->display_lock_fd = -1;
    d->button_uevent_fd = -1;
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
    if (!result)
        result = er_release_display(d);
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

int er_get_identity(const er_device *d, struct er_identity *identity)
{
    if (!d || !identity)
        return -EINVAL;
    *identity = (struct er_identity){0};
    char text[256];
    const char *serial_path = d->vendor == ER_KINDLE ? "/proc/usid" : "/mnt/onboard/.kobo/version";
    if (!er_read_line(serial_path, text, sizeof(text))) {
        if (d->vendor == ER_KOBO)
            text[strcspn(text, ",")] = 0;
        if (strlen(text) < sizeof(identity->serial))
            memcpy(identity->serial, text, strlen(text) + 1);
    }
    if (d->wifi_interface[0])
        snprintf(
            identity->wifi_interface, sizeof(identity->wifi_interface), "%s", d->wifi_interface);
    else {
        glob_t radios = {0};
        if (!glob("/sys/class/net/*/wireless", 0, NULL, &radios) && radios.gl_pathc == 1) {
            const char *name = radios.gl_pathv[0] + strlen("/sys/class/net/");
            size_t length = strcspn(name, "/");
            if (length < sizeof(identity->wifi_interface))
                memcpy(identity->wifi_interface, name, length);
        }
        globfree(&radios);
    }
    if (identity->wifi_interface[0]) {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/net/%s/address", identity->wifi_interface);
        if (!er_read_line(path, text, sizeof(text)) && strlen(text) == 17) {
            bool valid = true;
            for (size_t i = 0; i < 17; ++i)
                if (i % 3 == 2 ? text[i] != ':' : !isxdigit((unsigned char)text[i]))
                    valid = false;
            if (valid)
                memcpy(identity->wifi_mac, text, sizeof(identity->wifi_mac));
        }
    }
    return 0;
}

int er_supported(const er_device *d, enum er_capability cap)
{
    if (!d)
        return -EINVAL;
    if (!(d->info.capabilities & cap))
        return -EOPNOTSUPP;
    return (d->implemented & cap) ? 0 : -ENOSYS;
}
