#define _POSIX_C_SOURCE 200809L
#include "ereader_hal.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void on_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static const char *yes(bool value)
{
    return value ? "yes" : "no";
}

static const char *charge[] = {"unknown", "charging", "discharging", "full", "not-charging"};
static const char *usb_modes[] = {"disconnected", "connected", "charging", "data"};
static const char *actions[] = {"release", "press", "repeat", "move", "cancel"};
static const char *buttons[] = {
    "previous", "next", "power", "home", "back", "menu", "up", "down", "left", "right", "select"};
static void usage(void)
{
    fprintf(stderr,
            "Usage: ereader-hal models | info [MODEL] | capabilities | battery | usb | display\n"
            "       ereader-hal brightness [0..100] | warmth [0..100] | wifi [on|off]\n"
            "       ereader-hal light [on|off] | auto-brightness on|off\n"
            "       ereader-hal rotate 0|90|180|270\n"
            "       ereader-hal events [SECONDS] | hold hal|system SECONDS\n"
            "       ereader-hal wake SECONDS | cancel-wake | suspend [SECONDS] | wake-reason\n");
}

static int number(const char *text, int max, int *value)
{
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno || end == text || *end || n < 0 || n > max)
        return -EINVAL;
    *value = (int)n;
    return 0;
}

static void print_usb(const struct er_usb *usb)
{
    printf("usb=%s external-power=%s storage-mounted=%s storage-writable=%s\n",
           usb_modes[usb->state],
           yes(usb->external_power),
           yes(usb->storage_mounted),
           yes(usb->storage_writable));
}

static void print_event(const struct er_event *event)
{
    switch (event->type) {
    case ER_EVENT_BUTTON:
        printf("button %s %s\n",
               buttons[event->data.button.button],
               actions[event->data.button.action]);
        break;
    case ER_EVENT_TOUCH:
        printf("touch id=%d %s x=%d y=%d\n",
               event->data.touch.id,
               actions[event->data.touch.action],
               event->data.touch.x,
               event->data.touch.y);
        break;
    case ER_EVENT_COVER:
        printf("cover %s\n", event->data.cover_closed ? "closed" : "open");
        break;
    case ER_EVENT_ORIENTATION:
        printf("orientation %u\n", event->data.orientation);
        break;
    case ER_EVENT_BATTERY:
        printf("battery %d %s\n", event->data.battery.percent, charge[event->data.battery.state]);
        break;
    case ER_EVENT_LOW_BATTERY:
        printf("low-battery %d\n", event->data.battery.percent);
        break;
    case ER_EVENT_USB:
        print_usb(&event->data.usb);
        break;
    case ER_EVENT_PREPARE_SLEEP:
        puts("prepare-sleep");
        break;
    case ER_EVENT_RESUMED:
        printf("resumed reason=%d irq=%d\n", event->data.wake.reason, event->data.wake.irq);
        break;
    case ER_EVENT_SUSPEND_FAILED:
        printf("suspend-failed %s\n", strerror(-event->data.failure.error));
        break;
    case ER_EVENT_INPUT_RESET:
        puts("input-reset");
        break;
    case ER_EVENT_STATUS_ERROR:
        printf("status-error %s %s\n",
               er_capability_name(event->data.failure.capability),
               strerror(-event->data.failure.error));
        break;
    case ER_EVENT_WIFI:
        printf("wifi state=%d ssid=%s address=%s\n",
               event->data.wifi.state,
               event->data.wifi.ssid,
               event->data.wifi.address);
        break;
    case ER_EVENT_CAPABILITY: {
        const struct er_capability_change *change = &event->data.capability;
        printf("capability %s available=%s blocked=%s reason=%s (%s)\n",
               er_capability_name(change->capability),
               yes(change->info.available),
               yes(change->info.blocked),
               er_capability_reason_name(change->info.reason_code),
               change->info.reason);
    } break;
    }
}

static int lifecycle(const struct er_event *event, void *context)
{
    (void)context;
    print_event(event);
    return 0;
}

static void print_info(const struct er_info *info)
{
    printf("%-22s %s\n", "model:", info->model);
    printf("%-22s %s\n", "model codes:", info->codes);
    if (info->codenames[0])
        printf("%-22s %s\n", "codenames:", info->codenames);
    if (info->panel_width && info->panel_height)
        printf("%-22s %u x %u\n", "panel:", info->panel_width, info->panel_height);
    printf("%-22s %u\n", "dpi:", info->dpi);
    printf("%-22s %s\n", "suspend mode:", info->suspend_mode);
    puts("");

    const struct {
        const char *name;
        bool present;
    } hardware[] = {
        {"color", info->color},
        {"touch", info->touch},
        {"multitouch", info->features.multitouch},
        {"stylus", info->stylus},
        {"page buttons", info->page_buttons},
        {"navigation keys", info->features.navigation_keys},
        {"d-pad", info->features.dpad},
        {"firmware keyboard", info->features.firmware_keyboard},
        {"power button", info->power_button},
        {"cover", info->cover},
        {"orientation sensor", info->orientation_sensor},
        {"frontlight", (info->capabilities & ER_FRONTLIGHT) != 0},
        {"warmth", (info->capabilities & ER_WARMTH) != 0},
        {"light sensor", info->features.light_sensor},
        {"natural-light mixer", info->features.natural_light_mixer},
        {"native light off", info->features.native_light_off},
        {"auxiliary battery", info->features.auxiliary_battery},
        {"charging LED", info->features.charging_led},
    };
    for (size_t i = 0; i < sizeof(hardware) / sizeof(hardware[0]); ++i)
        printf("%-22s %s\n", hardware[i].name, yes(hardware[i].present));
}

static int capabilities(er_device *d)
{
    printf("%-18s %-10s %-12s %-10s %s\n",
           "Capability",
           "Supported",
           "Implemented",
           "Available",
           "Blocked");
    for (unsigned i = 0; i < ER_CAPABILITY_COUNT; ++i) {
        enum er_capability cap = (enum er_capability)(1u << i);
        struct er_capability_info info;
        int result = er_get_capability(d, cap, &info);
        if (result)
            return result;
        printf("%-18s %-10s %-12s %-10s %s\n",
               er_capability_name(cap),
               yes(info.supported),
               yes(info.implemented),
               yes(info.available),
               yes(info.blocked));
        if (info.steps)
            printf("  Range: %d..%d (%d steps)\n", info.minimum, info.maximum, info.steps);
        printf("  %s: %s\n\n", er_capability_reason_name(info.reason_code), info.reason);
    }
    return 0;
}

static int command(er_device *d, int argc, char **argv)
{
    const char *cmd = argv[1];
    int result, value;
    if (!strcmp(cmd, "info") && argc == 2) {
        print_info(er_get_info(d));
        return 0;
    }
    if (!strcmp(cmd, "capabilities") && argc == 2)
        return capabilities(d);
    if ((!strcmp(cmd, "brightness") || !strcmp(cmd, "warmth")) && argc <= 3) {
        bool warmth = !strcmp(cmd, "warmth");
        if (argc == 3) {
            result = number(argv[2], 100, &value);
            if (result)
                return result;
            return warmth ? er_set_warmth(d, value) : er_set_brightness(d, value);
        }
        result = warmth ? er_get_warmth(d, &value) : er_get_brightness(d, &value);
        if (!result)
            printf("%d\n", value);
        return result;
    }
    if ((!strcmp(cmd, "wifi") || !strcmp(cmd, "light") || !strcmp(cmd, "auto-brightness")) &&
        argc <= 3) {
        if (argc == 3) {
            if (strcmp(argv[2], "on") && strcmp(argv[2], "off"))
                return -EINVAL;
            bool enabled = !strcmp(argv[2], "on");
            if (!strcmp(cmd, "wifi"))
                return er_set_wifi(d, enabled);
            if (!strcmp(cmd, "light"))
                return er_set_frontlight(d, enabled);
            return er_set_auto_brightness(d, enabled);
        }
        if (!strcmp(cmd, "wifi")) {
            bool enabled;
            result = er_get_wifi(d, &enabled);
            if (!result)
                puts(enabled ? "on" : "off");
            return result;
        }
        if (!strcmp(cmd, "light")) {
            struct er_frontlight light;
            result = er_get_frontlight(d, &light);
            if (!result)
                printf("enabled=%s brightness=%d warmth=%d restore-brightness=%d\n",
                       yes(light.enabled),
                       light.brightness,
                       light.warmth,
                       light.restore_brightness);
            return result;
        }
        return -EINVAL;
    }
    if (!strcmp(cmd, "battery") && argc == 2) {
        struct er_battery battery;
        result = er_get_battery(d, &battery);
        if (!result)
            printf("percent: %d\nstate: %s\n", battery.percent, charge[battery.state]);
        return result;
    }
    if (!strcmp(cmd, "usb") && argc == 2) {
        struct er_usb usb;
        result = er_get_usb(d, &usb);
        if (!result)
            print_usb(&usb);
        return result;
    }
    if (!strcmp(cmd, "display") && argc == 2) {
        struct er_display display;
        result = er_get_display(d, &display);
        if (!result)
            printf("width: %u\nheight: %u\nbits-per-pixel: %u\nrotation: %u\n",
                   display.width,
                   display.height,
                   display.bits_per_pixel,
                   display.rotation);
        return result;
    }
    if (!strcmp(cmd, "rotate") && argc == 3) {
        result = number(argv[2], 270, &value);
        return result ? result : er_set_rotation(d, (unsigned)value);
    }
    if (!strcmp(cmd, "events") && argc <= 3) {
        int seconds = 0;
        if (argc == 3 && number(argv[2], 86400, &seconds))
            return -EINVAL;
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        time_t next_sample = 0;
        while (!interrupted) {
            struct er_event event;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec >= next_sample) {
                result = er_sample_status(d);
                if (result)
                    return result;
                next_sample = now.tv_sec + 1;
            }
            result = er_poll_event(d, &event, 1000);
            if (result < 0)
                return result;
            if (result)
                print_event(&event);
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (seconds && now.tv_sec - start.tv_sec >= seconds)
                break;
        }
        return 0;
    }
    if (!strcmp(cmd, "hold") && argc == 4) {
        if (strcmp(argv[2], "hal") && strcmp(argv[2], "system"))
            return -EINVAL;
        if (number(argv[3], 86400, &value) || !value)
            return -EINVAL;
        result = er_inhibit_suspend(d, !strcmp(argv[2], "system"));
        if (result)
            return result;
        puts("suspend inhibited");
        struct timespec duration = {.tv_sec = value};
        while (!interrupted && nanosleep(&duration, &duration) < 0 && errno == EINTR) {
        }
        return er_release_suspend(d);
    }
    if (!strcmp(cmd, "wake-reason") && argc == 2) {
        struct er_wake wake;
        result = er_get_wake_reason(d, &wake);
        if (!result)
            printf("reason=%d irq=%d\n", wake.reason, wake.irq);
        return result;
    }
    if (!strcmp(cmd, "cancel-wake") && argc == 2)
        return er_cancel_wake_alarm(d);
    if (!strcmp(cmd, "suspend") && argc == 2)
        return er_suspend(d);
    if ((!strcmp(cmd, "wake") || !strcmp(cmd, "suspend")) && argc == 3) {
        result = number(argv[2], 86400, &value);
        if (result || !value)
            return -EINVAL;
        return !strcmp(cmd, "wake") ? er_set_wake_alarm(d, (unsigned)value)
                                    : er_suspend_for(d, (unsigned)value);
    }
    return -EINVAL;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction action = {.sa_handler = on_signal};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage();
        return 0;
    }
    if (argc < 2) {
        usage();
        return 2;
    }
    if (argc == 2 && !strcmp(argv[1], "models")) {
        for (unsigned i = 0; er_model_name(i); ++i)
            puts(er_model_name(i));
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "info")) {
        for (unsigned i = 0; er_model_name(i); ++i) {
            if (strcmp(argv[2], er_model_name(i)))
                continue;
            struct er_info info;
            int result = er_get_model_info(i, &info);
            if (result)
                return 1;
            print_info(&info);
            return 0;
        }
        fprintf(stderr, "ereader-hal: unknown model: %s\n", argv[2]);
        return 1;
    }
    er_device *device;
    int result = er_open(&device);
    if (!result) {
        er_set_lifecycle_handler(device, lifecycle, NULL);
        result = command(device, argc, argv);
        int cleanup = er_close(device);
        if (cleanup) {
            fprintf(stderr, "ereader-hal cleanup: %s (%d)\n", strerror(-cleanup), -cleanup);
            if (!result)
                result = cleanup;
        }
    }
    if (result) {
        fprintf(stderr, "ereader-hal: %s (%d)\n", strerror(-result), -result);
        if (result == -EINVAL)
            usage();
        return 1;
    }
    return 0;
}
