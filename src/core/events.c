#define _GNU_SOURCE
#include "core/events.h"
#include "core/device.h"
#include "linux/evdev.h"
#include "linux/io.h"
#include <errno.h>
#include <string.h>
#include <time.h>

/* Linux UAPI value absent from the K4-era toolchain headers. */
#define ER_CLOCK_BOOTTIME ((clockid_t)7)

void er_input_reset(er_device *d)
{
    er_input_clear(d);
    d->queue_head = 0;
    d->queue_count = 1;
    d->queue[0] = (struct er_event){.type = ER_EVENT_INPUT_RESET};
}

void er_queue_event(er_device *d, const struct er_event *event)
{
    if (d->queue_count == ER_QUEUE_SIZE) {
        er_input_reset(d);
        return;
    }
    unsigned tail = (d->queue_head + d->queue_count) % ER_QUEUE_SIZE;
    d->queue[tail] = *event;
    ++d->queue_count;
}

void er_status_error(er_device *d, enum er_capability cap, int error)
{
    struct er_event event = {.type = ER_EVENT_STATUS_ERROR};
    event.data.failure.capability = cap;
    event.data.failure.error = error;
    er_queue_event(d, &event);
}

int er_watch_capabilities(er_device *d, uint32_t capabilities)
{
    if (!d || (capabilities & ~((1u << ER_CAPABILITY_COUNT) - 1)))
        return -EINVAL;
    d->watched_capabilities = capabilities;
    d->capability_valid = 0;
    d->wifi_valid = false;
    return 0;
}

static void sample_capabilities(er_device *d)
{
    for (unsigned i = 0; i < ER_CAPABILITY_COUNT; ++i) {
        enum er_capability cap = 1u << i;
        if (!(d->watched_capabilities & cap))
            continue;
        struct er_capability_info info;
        if (er_get_capability(d, cap, &info))
            continue;
        struct er_capability_info *last = &d->last_capability[i];
        if (!(d->capability_valid & cap) || info.supported != last->supported ||
            info.implemented != last->implemented || info.available != last->available ||
            info.blocked != last->blocked || info.error != last->error ||
            info.reason_code != last->reason_code || info.minimum != last->minimum ||
            info.maximum != last->maximum || info.steps != last->steps ||
            strcmp(info.reason, last->reason)) {
            struct er_event event = {.type = ER_EVENT_CAPABILITY};
            event.data.capability.capability = cap;
            event.data.capability.info = info;
            er_queue_event(d, &event);
            *last = info;
            d->capability_valid |= cap;
        }
        if (cap == ER_WIFI_STATUS && info.available) {
            struct er_wifi_status status;
            int result = er_get_wifi_status(d, &status);
            if (!result) {
                if (!d->wifi_valid || status.state != d->last_wifi.state ||
                    strcmp(status.ssid, d->last_wifi.ssid) ||
                    strcmp(status.address, d->last_wifi.address) ||
                    strcmp(status.network_id, d->last_wifi.network_id) ||
                    strcmp(status.bssid, d->last_wifi.bssid)) {
                    struct er_event event = {.type = ER_EVENT_WIFI};
                    event.data.wifi = status;
                    er_queue_event(d, &event);
                }
                d->last_wifi = status;
                d->wifi_valid = true;
            } else {
                er_status_error(d, ER_WIFI_STATUS, result);
                d->wifi_valid = false;
            }
        } else if (cap == ER_WIFI_STATUS) {
            d->wifi_valid = false;
        }
    }
}

int er_sample_status(er_device *d)
{
    if (!d)
        return -EINVAL;
    sample_capabilities(d);
    er_input_open(d);
    struct er_event event;
    struct er_battery battery;
    int result = er_get_battery(d, &battery);
    if (result)
        er_status_error(d, ER_BATTERY, result);
    else {
        if (!d->battery_valid || battery.percent != d->last_battery.percent ||
            battery.state != d->last_battery.state) {
            event.type = ER_EVENT_BATTERY;
            event.data.battery = battery;
            er_queue_event(d, &event);
        }
        if (battery.percent <= d->low_battery_threshold &&
            (!d->battery_valid || d->last_battery.percent > d->low_battery_threshold)) {
            event.type = ER_EVENT_LOW_BATTERY;
            event.data.battery = battery;
            er_queue_event(d, &event);
        }
        d->last_battery = battery;
        d->battery_valid = true;
    }
    struct er_usb usb;
    result = er_get_usb(d, &usb);
    if (result)
        er_status_error(d, ER_USB, result);
    else {
        if (!d->usb_valid || usb.state != d->last_usb.state ||
            usb.external_power != d->last_usb.external_power ||
            usb.storage_mounted != d->last_usb.storage_mounted ||
            usb.storage_writable != d->last_usb.storage_writable) {
            event.type = ER_EVENT_USB;
            event.data.usb = usb;
            er_queue_event(d, &event);
        }
        d->last_usb = usb;
        d->usb_valid = true;
    }
    bool closed;
    result = er_get_cover(d, &closed);
    if (result && result != -EOPNOTSUPP)
        er_status_error(d, ER_COVER, result);
    else if (!result) {
        if (!d->cover_valid || d->last_cover != closed) {
            event.type = ER_EVENT_COVER;
            event.data.cover_closed = closed;
            er_queue_event(d, &event);
        }
        d->last_cover = closed;
        d->cover_valid = true;
    }
    struct timespec boot, monotonic;
    if (!clock_gettime(ER_CLOCK_BOOTTIME, &boot) && !clock_gettime(CLOCK_MONOTONIC, &monotonic)) {
        int64_t delta = ((int64_t)boot.tv_sec - monotonic.tv_sec) * 1000 +
                        (boot.tv_nsec - monotonic.tv_nsec) / 1000000;
        if (d->polling_started && delta - d->sleep_delta_ms > 100) {
            event.type = ER_EVENT_RESUMED;
            er_get_wake_reason(d, &event.data.wake);
            er_queue_event(d, &event);
        }
        d->sleep_delta_ms = delta;
    }
    d->polling_started = true;
    return 0;
}

int er_set_low_battery_threshold(er_device *d, int percent)
{
    if (!d || percent < 0 || percent > 100)
        return -EINVAL;
    d->low_battery_threshold = percent;
    d->battery_valid = false;
    return 0;
}

int er_poll_event(er_device *d, struct er_event *event, int timeout_ms)
{
    if (!d || !event || timeout_ms < -1)
        return -EINVAL;
    int64_t deadline = timeout_ms < 0 ? INT64_MAX : er_now_ms() + timeout_ms;
    bool polled = false;
    for (;;) {
        if (d->queue_count) {
            *event = d->queue[d->queue_head];
            d->queue_head = (d->queue_head + 1) % ER_QUEUE_SIZE;
            --d->queue_count;
            return 1;
        }
        int wait = -1;
        if (timeout_ms >= 0) {
            int64_t remaining = deadline - er_now_ms();
            if (remaining <= 0) {
                if (polled)
                    return 0;
                wait = 0;
            } else {
                wait = (int)remaining;
            }
        }
        int result = er_input_poll(d, wait);
        polled = true;
        if (result && result != -EINTR)
            return result;
    }
}
