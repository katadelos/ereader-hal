#define _GNU_SOURCE
#include "linux/io.h"
#include "usb.h"
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int er_kindle_usb_policy(int *policy)
{
    char text[32];
    char *args[] = {"/usr/bin/lipc-get-prop", "com.lab126.volumd", "useUsbForNetwork", NULL};
    int result = er_run_timeout(args, text, sizeof(text), 2500);
    if (!result)
        result = er_parse_int(text, policy);
    if (!result && *policy != 0 && *policy != 1)
        result = -EOPNOTSUPP;
    return result;
}

int er_kindle_usb_mode(uint32_t *mode)
{
    *mode = 0;
    glob_t paths = {0};
    int found = glob("/sys/class/udc/*/uevent", 0, NULL, &paths);
    if (found && found != GLOB_NOMATCH) {
        globfree(&paths);
        return -EIO;
    }
    if (paths.gl_pathc > 1) {
        globfree(&paths);
        return -ENOTUNIQ;
    }
    if (paths.gl_pathc == 1) {
        char text[1024];
        int result = er_read_text(paths.gl_pathv[0], text, sizeof(text));
        globfree(&paths);
        if (result)
            return result;
        char *save;
        for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (strncmp(line, "USB_UDC_DRIVER=", 15))
                continue;
            const char *driver = line + 15;
            if (!strcmp(driver, "g_ether")) {
                if (!access("/sys/class/net/usb0", F_OK))
                    *mode = ER_USB_MODE_NETWORK;
            } else if (!strcmp(driver, "g_file_storage") || !strcmp(driver, "g_mass_storage"))
                *mode = ER_USB_MODE_FILESTORAGE;
            else if (driver[0])
                return -EOPNOTSUPP;
        }
        return 0;
    }
    globfree(&paths);
    /* Kindle 4/PW-era kernels predate the UDC class; their stock modules own the sole controller.
     */
    bool network = access("/sys/class/net/usb0", F_OK) == 0;
    bool storage = access("/sys/module/g_file_storage", F_OK) == 0 ||
                   access("/sys/module/g_mass_storage", F_OK) == 0;
    if (network && storage)
        return -EBUSY;
    if (network)
        *mode = ER_USB_MODE_NETWORK;
    else if (storage)
        *mode = ER_USB_MODE_FILESTORAGE;
    return 0;
}

static int event(const char *name)
{
    char *args[] = {"/usr/bin/lipc-send-event", "com.lab126.hal", (char *)name, NULL};
    return er_run_timeout(args, NULL, 0, 2500);
}

int er_kindle_usb_set_policy(int policy)
{
    char *args[] = {"/usr/bin/lipc-set-prop",
                    "com.lab126.volumd",
                    "useUsbForNetwork",
                    policy ? "1" : "0",
                    NULL};
    return er_run_timeout(args, NULL, 0, 2500);
}

int er_kindle_usb_claim(struct er_kindle_usb *state, bool *reenumerated)
{
    int result = er_kindle_usb_service_ready();
    if (!result && !state->policy_saved) {
        result = er_kindle_usb_policy(&state->original_policy);
        state->policy_saved = !result;
    }
    uint32_t mode = 0;
    if (!result)
        result = er_kindle_usb_mode(&mode);
    if (result)
        return result;
    /* A timed-out property call may still have reached firmware. Retain the saved policy. */
    state->policy_changed = state->original_policy != 1;
    result = er_kindle_usb_set_policy(1);
    if (result || mode == ER_USB_MODE_NETWORK)
        return result;
    result = event("usbUnconfigured");
    if (!result)
        result = er_sleep_ms(250);
    if (!result)
        result = event("usbPlugOut");
    if (result)
        return result;
    *reenumerated = true;
    int64_t deadline = er_now_ms() + 10000;
    unsigned settled = 0;
    while (er_now_ms() < deadline) {
        result = er_kindle_usb_mode(&mode);
        if (!result && mode == ER_USB_MODE_NETWORK) {
            if (++settled == 3)
                return 0;
        } else
            settled = 0;
        result = er_sleep_ms(100);
        if (result)
            return result;
    }
    return -ETIMEDOUT;
}
