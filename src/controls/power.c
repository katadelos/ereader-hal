#define _GNU_SOURCE
#include "controls/power.h"
#include "backends/kindle/lipc_api.h"
#include "backends/network.h"
#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <linux/rtc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define ER_POWER_LOCK "/var/run/ereader-hal-suspend.lock"
#define ER_DISPLAY_LOCK "/var/run/ereader-hal-display.lock"

int er_rtc_path(char *path, size_t size, const char *attribute)
{
    glob_t clocks = {0};
    int result = -ENODEV, priority = -1;
    if (glob("/sys/class/rtc/rtc*/name", 0, NULL, &clocks)) {
        globfree(&clocks);
        return result;
    }
    const char *selected = NULL;
    bool ambiguous = false;
    for (size_t i = 0; i < clocks.gl_pathc; ++i) {
        char name[64];
        if (er_read_line(clocks.gl_pathv[i], name, sizeof(name)))
            continue;
        /* MAX77696 alarm0 also serves firmware hibernation; pmic_rtc is
         * the wake-capable external clock on older i.MX/Netronix hardware. */
        int candidate = !strcmp(name, "max77696-rtc.1") ? 2 : !strcmp(name, "pmic_rtc") ? 1 : 0;
        if (candidate > priority) {
            selected = clocks.gl_pathv[i];
            priority = candidate;
            ambiguous = false;
        } else if (candidate == priority) {
            ambiguous = true;
        }
    }
    if (ambiguous)
        result = -EBUSY;
    else if (selected) {
        size_t prefix = strlen(selected) - strlen("name");
        int length = snprintf(path, size, "%.*s%s", (int)prefix, selected, attribute);
        result = length < 0 || (size_t)length >= size ? -ENAMETOOLONG : 0;
    }
    globfree(&clocks);
    return result;
}

unsigned er_rtc_max_seconds(const er_device *d)
{
    char path[256], name[64];
    if (d && d->vendor == ER_KOBO && !er_rtc_path(path, sizeof(path), "name") &&
        !er_read_line(path, name, sizeof(name)) && !strcmp(name, "pmic_rtc"))
        return UINT16_MAX; /* Netronix MSP430 has a 16-bit seconds countdown. */
    return 86400;
}

struct wake_clock {
    int fd;
    bool missing_enabled, netronix;
};

static int rtc_open(const er_device *d, struct wake_clock *clock)
{
    *clock = (struct wake_clock){.fd = -1};
    char path[256], name[64], node[64];
    int result = er_rtc_path(path, sizeof(path), "name");
    unsigned number;
    if (result)
        return result;
    if (sscanf(path, "/sys/class/rtc/rtc%u/name", &number) != 1)
        return -EINVAL;
    result = er_read_line(path, name, sizeof(name));
    if (result)
        return result;
    clock->missing_enabled = d->vendor == ER_KINDLE && !strcmp(name, "pmic_rtc");
    clock->netronix = d->vendor == ER_KOBO && !strcmp(name, "pmic_rtc");
    snprintf(node, sizeof(node), "/dev/rtc%u", number);
    clock->fd = open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    return clock->fd < 0 ? -errno : 0;
}

static time_t rtc_epoch(const struct rtc_time *time)
{
    struct tm value = {.tm_sec = time->tm_sec,
                       .tm_min = time->tm_min,
                       .tm_hour = time->tm_hour,
                       .tm_mday = time->tm_mday,
                       .tm_mon = time->tm_mon,
                       .tm_year = time->tm_year};
    time_t seconds = timegm(&value);
    /* timegm normalizes malformed RTC fields instead of rejecting them. */
    if (value.tm_sec != time->tm_sec || value.tm_min != time->tm_min ||
        value.tm_hour != time->tm_hour || value.tm_mday != time->tm_mday ||
        value.tm_mon != time->tm_mon || value.tm_year != time->tm_year)
        return -1;
    return seconds;
}

static int rtc_now(const struct wake_clock *clock, time_t *seconds)
{
    struct rtc_time time = {0};
    if (ioctl(clock->fd, RTC_RD_TIME, &time))
        return -errno;
    *seconds = rtc_epoch(&time);
    return *seconds < 0 ? -EIO : 0;
}

static int rtc_alarm(const struct wake_clock *clock, time_t *deadline, bool *enabled)
{
    struct rtc_wkalrm alarm = {0};
    if (ioctl(clock->fd, RTC_WKALM_RD, &alarm))
        return -errno;
    *deadline = rtc_epoch(&alarm.time);
    *enabled = alarm.enabled != 0;
    if (clock->missing_enabled) {
        /* MC13892's stock RTC driver omits alarm.enabled. Its alarm register
         * has a disable bit and an invalid time-of-day sentinel at reset. */
        struct {
            int reg;
            uint32_t value, mask;
        } info = {.reg = 21};
        int fd = open("/dev/pmic", O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return -errno;
        int result = ioctl(fd, _IOWR('P', 0xa0, uint32_t), &info) ? -errno : 0;
        close(fd);
        if (result)
            return result;
        time_t now;
        result = rtc_now(clock, &now);
        if (result)
            return result;
        bool programmed = !(info.value & (1u << 20)) && (info.value & 0x1ffff) < 86400;
        if (programmed && *deadline < 0)
            return -EIO;
        *enabled = programmed && *deadline > now;
    }
    if (*enabled && *deadline < 0)
        return -EIO;
    if (clock->netronix && *enabled) {
        time_t now;
        int result = rtc_now(clock, &now);
        if (result)
            return result;
        /* The firmware leaves enabled latched after its one-shot countdown. */
        *enabled = *deadline > now;
    }
    return 0;
}

static int rtc_program(const struct wake_clock *clock, time_t deadline, bool enabled)
{
    if (clock->netronix && !enabled) {
        /* This firmware ignores disabled WKALM_SET requests. Disable the
         * countdown explicitly, then replace its stale enabled timestamp with
         * a past one-shot alarm; the driver does not arm past deadlines. */
        if (ioctl(clock->fd, RTC_AIE_OFF, 0))
            return -errno;
        time_t now;
        int result = rtc_now(clock, &now);
        if (result || now <= 0)
            return result ? result : -EIO;
        deadline = now - 1;
        enabled = true;
    }
    struct tm value;
    if (!gmtime_r(&deadline, &value))
        return -ERANGE;
    struct rtc_wkalrm alarm = {.enabled = enabled};
    alarm.time = (struct rtc_time){.tm_sec = value.tm_sec,
                                   .tm_min = value.tm_min,
                                   .tm_hour = value.tm_hour,
                                   .tm_mday = value.tm_mday,
                                   .tm_mon = value.tm_mon,
                                   .tm_year = value.tm_year,
                                   .tm_wday = value.tm_wday,
                                   .tm_yday = value.tm_yday};
    if (ioctl(clock->fd, RTC_WKALM_SET, &alarm))
        return -errno;
    if (clock->missing_enabled && ioctl(clock->fd, enabled ? RTC_AIE_ON : RTC_AIE_OFF, 0))
        return -errno;
    return 0;
}

static bool rtc_woke(const struct wake_clock *clock)
{
    if (clock->netronix) {
        /* Battery/resume handling may consume the MSP430 status register.
         * ntx_io keeps its alarm cause latched until this ioctl reads it. */
        int fd = open("/dev/ntx_io", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            return false;
        unsigned long flag = 0;
        int result = ioctl(fd, 123, &flag);
        close(fd);
        return !result && flag;
    }
    unsigned long events = 0;
    return read(clock->fd, &events, sizeof(events)) == sizeof(events) && (events & RTC_AF);
}

int er_rtc_probe(const er_device *d)
{
    struct wake_clock clock;
    int result = rtc_open(d, &clock);
    if (!result) {
        time_t deadline, now;
        bool enabled;
        result = rtc_now(&clock, &now);
        if (!result)
            result = rtc_alarm(&clock, &deadline, &enabled);
        if (!result && enabled)
            result = -EBUSY;
        close(clock.fd);
    }
    return result;
}

static int suspend_success_count(uint64_t *count)
{
    FILE *stats = fopen("/sys/kernel/debug/suspend_stats", "r");
    if (!stats)
        return -errno;
    char line[256];
    int result = -ENODATA;
    unsigned long long value;
    while (fgets(line, sizeof(line), stats)) {
        if (sscanf(line, "success: %llu", &value) == 1) {
            *count = value;
            result = 0;
            break;
        }
    }
    fclose(stats);
    return result;
}

int er_get_last_sleep_ms(const er_device *d, uint64_t *milliseconds)
{
    if (!d || !milliseconds)
        return -EINVAL;
    *milliseconds = d->last_sleep_ms;
    return 0;
}

int er_get_last_sleep_gap_ms(const er_device *d, uint64_t *milliseconds)
{
    if (!d || !milliseconds)
        return -EINVAL;
    *milliseconds = d->last_sleep_gap_ms;
    return 0;
}

int er_get_last_wake(const er_device *d, struct er_wake *wake)
{
    if (!d || !wake)
        return -EINVAL;
    *wake = d->last_sleep_ms ? d->last_wake : (struct er_wake){ER_WAKE_UNKNOWN, -1};
    return 0;
}

static int wake_interrupt_count(enum er_wake_reason kind, uint64_t *total)
{
    FILE *interrupts = fopen("/proc/interrupts", "r");
    if (!interrupts)
        return -errno;
    char line[1024];
    bool found = false;
    *total = 0;
    while (fgets(line, sizeof(line), interrupts)) {
        bool matching = kind == ER_WAKE_RTC ? strcasestr(line, "rtc") != NULL
                        : kind == ER_WAKE_POWER
                            ? strcasestr(line, "pwrkey") || strcasestr(line, "pwr-shortpush") ||
                                  strcasestr(line, "pwr-midpush") ||
                                  strcasestr(line, "pwr-longpush") ||
                                  strcasestr(line, "onkey_press") || strcasestr(line, "onkey_rel")
                            : strcasestr(line, "hall") != NULL;
        if (!matching)
            continue;
        char *cursor = strchr(line, ':');
        if (!cursor)
            continue;
        ++cursor;
        for (;;) {
            char *end;
            unsigned long long count = strtoull(cursor, &end, 10);
            if (end == cursor)
                break;
            *total += count;
            cursor = end;
            found = true;
        }
    }
    fclose(interrupts);
    return found ? 0 : -ENOENT;
}

int er_retain_display(er_device *d)
{
    if (!d)
        return -EINVAL;
    if (d->vendor != ER_KINDLE)
        return -EOPNOTSUPP;
    if (d->display_lock_fd >= 0)
        return 0;
    int fd = open(ER_DISPLAY_LOCK, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -errno;
    int result = flock(fd, LOCK_EX | LOCK_NB) ? -errno : 0;
    int previous = 0;
    if (!result)
        result = er_kindle_get("com.lab126.powerd", "preventScreenSaver", &previous);
    if (!result && !previous)
        result = er_kindle_set("com.lab126.powerd", "preventScreenSaver", 1);
    if (result) {
        close(fd);
        return result;
    }
    d->display_previous = previous;
    d->display_lock_fd = fd;
    return 0;
}

int er_release_display(er_device *d)
{
    if (!d)
        return -EINVAL;
    if (d->display_lock_fd < 0)
        return 0;
    int current, result = er_kindle_get("com.lab126.powerd", "preventScreenSaver", &current);
    /* Do not overwrite a policy another owner deliberately changed. */
    if (!result && current == 1 && d->display_previous != 1)
        result = er_kindle_set("com.lab126.powerd", "preventScreenSaver", d->display_previous);
    if (result)
        return result;
    close(d->display_lock_fd);
    d->display_lock_fd = -1;
    return 0;
}

static int lock_power(int operation, bool create)
{
    int flags = create ? O_RDWR | O_CREAT : O_RDONLY;
    int fd = open(ER_POWER_LOCK, flags | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -errno;
    if (flock(fd, operation | LOCK_NB) < 0) {
        int result = -errno;
        close(fd);
        return result;
    }
    return fd;
}

static int probe_power_lock(int operation, char *reason, size_t size)
{
    int fd = lock_power(operation, false);
    if (fd >= 0) {
        close(fd);
        return 0;
    }
    if (fd == -ENOENT)
        return 0;
    if (fd == -EWOULDBLOCK) {
        snprintf(reason, size, "Another HAL client holds a conflicting suspend lock");
        return -EBUSY;
    }
    return fd;
}

static int awake_blocker(const er_device *d, char *reason, size_t size)
{
    if (d->awake_until_ms > er_now_ms()) {
        snprintf(reason, size, "This handle holds an awake lease");
        return -EBUSY;
    }
    char locks[4096];
    if (!er_read_text("/sys/power/wake_lock", locks, sizeof(locks)) &&
        strstr(locks, "ereader-hal-awake-")) {
        snprintf(reason, size, "A HAL client holds an awake lease");
        return -EBUSY;
    }
    return 0;
}

int er_suspend_blocker(const er_device *d, char *reason, size_t size)
{
    if (!strcmp(d->info.suspend_mode, "freeze")) {
        uint64_t count;
        int result = suspend_success_count(&count);
        if (result) {
            snprintf(
                reason, size, "Kernel suspend statistics are unavailable: %s", strerror(-result));
            return result;
        }
    }
    char test[128];
    if (!er_read_text("/sys/power/pm_test", test, sizeof(test)) && !strstr(test, "[none]")) {
        snprintf(reason, size, "Kernel suspend test mode is enabled");
        return -EBUSY;
    }
    int awake = awake_blocker(d, reason, size);
    if (awake)
        return awake;
    if (d->inhibit_count) {
        snprintf(reason, size, "This handle holds a suspend inhibitor");
        return -EBUSY;
    }
    int result = probe_power_lock(LOCK_EX, reason, size);
    if (result)
        return result;
    if (d->vendor == ER_KOBO) {
        result = er_kobo_wifi_suspend_blocker(d, reason, size);
        if (result == -EBUSY) {
            struct er_capability_info info = {0};
            result = er_kobo_wifi_probe(d, &info);
            if (result)
                snprintf(reason, size, "Cannot restore Wi-Fi after suspend: %s", info.reason);
        }
        if (result)
            return result;
    }
    FILE *sources = fopen("/sys/kernel/debug/wakeup_sources", "r");
    if (!sources) {
        snprintf(reason, size, "No HAL inhibitor; kernel wake-source reporting unavailable");
        return 0;
    }
    char line[1024];
    unsigned active_column = 0, column = 0;
    if (fgets(line, sizeof(line), sources)) {
        char *save;
        for (char *word = strtok_r(line, " \t\r\n", &save); word;
             word = strtok_r(NULL, " \t\r\n", &save), ++column)
            if (!strcmp(word, "active_since"))
                active_column = column;
    }
    if (!active_column) {
        fclose(sources);
        snprintf(reason, size, "Kernel wake-source format has no active_since field");
        return -EPROTO;
    }
    while (fgets(line, sizeof(line), sources)) {
        char *save, *name = strtok_r(line, " \t\r\n", &save), *value = NULL;
        for (unsigned i = 0; i < active_column; ++i)
            value = strtok_r(NULL, " \t\r\n", &save);
        if (!name || !value || !strtoull(value, NULL, 10))
            continue;
        snprintf(reason, size, "Active kernel wake source: %.90s", name);
        fclose(sources);
        return -EBUSY;
    }
    fclose(sources);
    return 0;
}

int er_inhibit_suspend(er_device *d, bool system_scope)
{
    int result = er_supported(d, system_scope ? ER_INHIBIT_SYSTEM : ER_INHIBIT_HAL);
    if (result)
        return result;
    if (d->inhibit_count) {
        if (d->system_inhibit != system_scope)
            return -EINVAL;
        if (d->inhibit_count == UINT_MAX)
            return -EOVERFLOW;
        ++d->inhibit_count;
        return 0;
    }
    int fd = lock_power(LOCK_SH, true);
    if (fd < 0)
        return fd;
    if (system_scope) {
        snprintf(d->wake_lock_name,
                 sizeof(d->wake_lock_name),
                 "ereader-hal-%ld-%p",
                 (long)getpid(),
                 (void *)d);
        result = er_write_text("/sys/power/wake_lock", d->wake_lock_name);
        if (result) {
            close(fd);
            return result;
        }
    }
    d->inhibit_fd = fd;
    d->inhibit_count = 1;
    d->system_inhibit = system_scope;
    return 0;
}

int er_release_suspend(er_device *d)
{
    if (!d || !d->inhibit_count)
        return -EINVAL;
    if (d->inhibit_count > 1) {
        --d->inhibit_count;
        return 0;
    }
    if (d->system_inhibit) {
        int result = er_write_text("/sys/power/wake_unlock", d->wake_lock_name);
        if (result)
            return result;
    }
    close(d->inhibit_fd);
    d->inhibit_fd = -1;
    d->inhibit_count = 0;
    d->system_inhibit = false;
    return 0;
}

int er_set_lifecycle_handler(er_device *d, er_lifecycle_handler handler, void *context)
{
    if (!d)
        return -EINVAL;
    d->lifecycle = handler;
    d->lifecycle_context = context;
    return 0;
}

int er_get_wake_reason(const er_device *d, struct er_wake *wake)
{
    if (!d || !wake)
        return -EINVAL;
    *wake = (struct er_wake){ER_WAKE_UNKNOWN, -1};
    int irq;
    if (er_read_int("/sys/power/pm_wakeup_irq", &irq))
        return 0;
    wake->irq = irq;
    FILE *interrupts = fopen("/proc/interrupts", "r");
    if (!interrupts)
        return 0;
    char line[512];
    while (fgets(line, sizeof(line), interrupts)) {
        int number;
        if (sscanf(line, " %d:", &number) != 1 || number != irq)
            continue;
        if (strcasestr(line, "rtc"))
            wake->reason = ER_WAKE_RTC;
        else if (strcasestr(line, "pwrkey"))
            wake->reason = ER_WAKE_POWER;
        else if (strcasestr(line, "hall"))
            wake->reason = ER_WAKE_COVER;
        break;
    }
    fclose(interrupts);
    return 0;
}

int er_set_wake_alarm(const er_device *d, unsigned seconds)
{
    if (!d || !seconds)
        return -EINVAL;
    if (seconds > er_rtc_max_seconds(d))
        return -ERANGE;
    int result = er_supported(d, ER_RTC_WAKE);
    struct wake_clock clock = {.fd = -1};
    if (!result)
        result = rtc_open(d, &clock);
    time_t now, deadline;
    bool enabled;
    if (!result)
        result = rtc_alarm(&clock, &deadline, &enabled);
    if (!result && enabled)
        result = -EBUSY;
    if (!result)
        result = rtc_now(&clock, &now);
    if (!result)
        result = rtc_program(&clock, now + seconds, true);
    if (clock.fd >= 0)
        close(clock.fd);
    return result;
}

int er_cancel_wake_alarm(const er_device *d)
{
    int result = er_supported(d, ER_RTC_WAKE);
    struct wake_clock clock = {.fd = -1};
    if (!result)
        result = rtc_open(d, &clock);
    if (!result)
        result = rtc_program(&clock, 0, false);
    if (clock.fd >= 0)
        close(clock.fd);
    return result;
}

static int notify(er_device *d, enum er_event_type type, int error)
{
    if (!d->lifecycle)
        return 0;
    struct er_event event = {.type = type};
    if (type == ER_EVENT_RESUMED)
        event.data.wake = d->last_wake;
    if (type == ER_EVENT_SUSPEND_FAILED) {
        event.data.failure.capability = ER_SUSPEND;
        event.data.failure.error = error;
    }
    return d->lifecycle(&event, d->lifecycle_context);
}

static int suspend_device(er_device *d, unsigned seconds)
{
    int result = er_supported(d, ER_SUSPEND);
    if (result)
        return result;
    d->last_sleep_ms = 0;
    d->last_sleep_gap_ms = 0;
    d->last_wake = (struct er_wake){ER_WAKE_UNKNOWN, -1};
    uint64_t rtc_before = 0, rtc_after = 0, power_before = 0, power_after = 0;
    uint64_t success_before = 0, success_after = 0;
    bool rtc_counted = false, power_counted = false;
    bool freeze = !strcmp(d->info.suspend_mode, "freeze");
    int64_t mono_before = 0, mono_after = 0, boot_before = 0, boot_after = 0;
    uint64_t power_events_before = 0;
    char reason[128];
    int lock = -1;
    struct wake_clock clock = {.fd = -1};
    time_t armed = 0, rtc_before_time = 0, rtc_after_time = 0;
    bool kernel_clock = true;
    bool did_suspend = false, alarm_set = false, radio_restore = false, rtc_fired = false;
    result = er_suspend_blocker(d, reason, sizeof(reason));
    if (result)
        goto done;
    lock = lock_power(LOCK_EX, true);
    if (lock < 0) {
        result = lock;
        goto done;
    }
    result = awake_blocker(d, reason, sizeof(reason));
    if (result)
        goto done;
    if (d->vendor == ER_KOBO) {
        result = er_kobo_wifi_suspend_prepare(d, &radio_restore);
        if (result)
            goto done;
    }
    result = rtc_open(d, &clock);
    if (result)
        goto done;
    rtc_woke(&clock); /* Discard a wake flag from a previous, unrelated cycle. */
    if (seconds) {
        time_t existing, now;
        bool enabled;
        result = rtc_alarm(&clock, &existing, &enabled);
        if (!result && enabled)
            result = -EBUSY;
        if (!result)
            result = rtc_now(&clock, &now);
        if (result)
            goto done;
        armed = now + seconds;
        alarm_set = true; /* Clean up even if the driver partially applies it. */
        result = rtc_program(&clock, armed, true);
        if (result)
            goto done;
    }
    result = notify(d, ER_EVENT_PREPARE_SLEEP, 0);
    if (result > 0)
        result = -EINVAL;
    if (result)
        goto done;
    /* Pending input belongs to the awake period, not the coming wake cause. */
    er_input_poll(d, 0);
    power_events_before = d->power_event_count;
    rtc_counted = wake_interrupt_count(ER_WAKE_RTC, &rtc_before) == 0;
    power_counted = wake_interrupt_count(ER_WAKE_POWER, &power_before) == 0;
    result = freeze ? suspend_success_count(&success_before) : 0;
    mono_before = er_now_ms();
    if (!result) {
        int sampled = er_boot_ms(&boot_before);
        if (sampled == -EOPNOTSUPP && !freeze)
            kernel_clock = false;
        else
            result = sampled;
    }
    if (!result)
        result = rtc_now(&clock, &rtc_before_time);
    /* Preparation and the lifecycle callback can outlast a short alarm. Never
     * enter sleep after its wake interrupt has already occurred. */
    if (!result && seconds && armed <= rtc_before_time)
        result = -ETIMEDOUT;
    if (result)
        goto done;
    result = er_write_text("/sys/power/state", d->info.suspend_mode);
    int measured = kernel_clock ? er_boot_ms(&boot_after) : 0;
    mono_after = er_now_ms();
    if (!measured)
        measured = rtc_now(&clock, &rtc_after_time);
    if (!measured && freeze)
        measured = suspend_success_count(&success_after);
    er_input_poll(d, 0);
    rtc_fired = rtc_woke(&clock);
    int64_t elapsed = kernel_clock ? boot_after - boot_before
                                   : (int64_t)(rtc_after_time - rtc_before_time) * 1000;
    int64_t gap = elapsed - (mono_after - mono_before);
    /* Legacy RTC-only clocks have one-second precision. A completed syscall
     * plus a stopped monotonic clock verifies deep sleep; freeze uses stats. */
    bool confirmed = freeze ? success_after > success_before : gap >= 100;
    did_suspend = !result && !measured && elapsed > 0 && confirmed;
    if (did_suspend) {
        d->last_sleep_ms = (uint64_t)elapsed;
        d->last_sleep_gap_ms = gap > 0 ? (uint64_t)gap : 0;
    }
    if (!result)
        result = measured ? measured : (did_suspend ? 0 : -EAGAIN);
done:
    if (alarm_set && clock.fd >= 0) {
        time_t current;
        bool enabled;
        int cleanup = rtc_alarm(&clock, &current, &enabled);
        if (!cleanup && current == armed)
            cleanup = rtc_program(&clock, 0, false);
        else if (!cleanup && enabled)
            cleanup = -EBUSY;
        if (cleanup)
            result = cleanup;
    }
    if (clock.fd >= 0)
        close(clock.fd);
    if (radio_restore) {
        int restored = er_kobo_wifi_set(d, true);
        if (restored)
            result = restored;
    }
    if (lock >= 0)
        close(lock);
    if (did_suspend) {
        er_get_wake_reason(d, &d->last_wake);
        if (d->power_event_count > power_events_before ||
            (power_counted && !wake_interrupt_count(ER_WAKE_POWER, &power_after) &&
             power_after > power_before))
            d->last_wake.reason = ER_WAKE_POWER;
        else if (d->last_wake.reason == ER_WAKE_UNKNOWN &&
                 (rtc_fired || (rtc_counted && !wake_interrupt_count(ER_WAKE_RTC, &rtc_after) &&
                                rtc_after > rtc_before)))
            d->last_wake.reason = ER_WAKE_RTC;
        d->polling_started = false; /* Do not rediscover this same resume in the event poller. */
        notify(d, ER_EVENT_RESUMED, 0);
    } else
        notify(d, ER_EVENT_SUSPEND_FAILED, result);
    return result;
}

int er_suspend(er_device *d)
{
    return suspend_device(d, 0);
}
int er_suspend_for(er_device *d, unsigned seconds)
{
    if (!d || !seconds)
        return -EINVAL;
    if (seconds > er_rtc_max_seconds(d))
        return -ERANGE;
    return suspend_device(d, seconds);
}

/* Older Kindle kernels expose no Android wake-lock interface. Keep their stock
 * idle timer alive only while the caller's lease is active. No persistent powerd
 * flag is changed; process exit also stops renewal. */
struct er_powerd_awake {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int64_t until_ms;
    int fd;
    bool stop;
};

static bool powerd_only_awake(const er_device *d)
{
    return d->vendor == ER_KINDLE && access("/sys/power/wake_lock", F_OK) < 0 && errno == ENOENT &&
           access("/sys/power/wake_unlock", F_OK) < 0 && errno == ENOENT;
}

static void *renew_powerd_idle(void *context)
{
    struct er_powerd_awake *lease = context;
    pthread_mutex_lock(&lease->mutex);
    while (!lease->stop) {
        int64_t now = er_now_ms();
        if (now >= lease->until_ms) {
            if (lease->fd >= 0) {
                close(lease->fd);
                lease->fd = -1;
            }
            pthread_cond_wait(&lease->changed, &lease->mutex);
            continue;
        }
        int64_t next = now + 15000;
        if (next > lease->until_ms)
            next = lease->until_ms;
        struct timespec timeout = {next / 1000, (next % 1000) * 1000000};
        int result = pthread_cond_timedwait(&lease->changed, &lease->mutex, &timeout);
        if (result == ETIMEDOUT && !lease->stop && er_now_ms() < lease->until_ms) {
            if (er_kindle_set("com.lab126.powerd", "touchScreenSaverTimeout", 1))
                lease->until_ms = 0;
        }
    }
    if (lease->fd >= 0)
        close(lease->fd);
    pthread_mutex_unlock(&lease->mutex);
    return NULL;
}

/* Takes ownership of fd on success. The shared lock blocks HAL suspend calls
 * from other handles until release or expiry. */
static int renew_powerd_awake(er_device *d, unsigned seconds, int fd)
{
    int64_t until = er_now_ms() + (int64_t)seconds * 1000;
    struct er_powerd_awake *lease = d->powerd_awake;
    if (lease) {
        pthread_mutex_lock(&lease->mutex);
        if (lease->fd >= 0)
            close(lease->fd);
        lease->fd = fd;
        if (until > lease->until_ms)
            lease->until_ms = until;
        d->awake_until_ms = lease->until_ms;
        pthread_cond_signal(&lease->changed);
        pthread_mutex_unlock(&lease->mutex);
        return 0;
    }
    lease = calloc(1, sizeof(*lease));
    if (!lease)
        return -ENOMEM;
    int result = pthread_mutex_init(&lease->mutex, NULL);
    if (result)
        goto free_lease;
    pthread_condattr_t attr;
    result = pthread_condattr_init(&attr);
    if (result)
        goto destroy_mutex;
    result = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!result)
        result = pthread_cond_init(&lease->changed, &attr);
    pthread_condattr_destroy(&attr);
    if (result)
        goto destroy_mutex;
    lease->fd = fd;
    lease->until_ms = until;
    result = pthread_create(&lease->thread, NULL, renew_powerd_idle, lease);
    if (result) {
        pthread_cond_destroy(&lease->changed);
        goto destroy_mutex;
    }
    d->powerd_awake = lease;
    d->awake_until_ms = until;
    return 0;
destroy_mutex:
    pthread_mutex_destroy(&lease->mutex);
free_lease:
    free(lease);
    return -result;
}

int er_awake_probe(const er_device *d, char *reason, size_t size)
{
    int result = er_supported(d, ER_AWAKE_LEASE);
    if (result)
        return result;
    if (!powerd_only_awake(d) &&
        (access("/sys/power/wake_lock", W_OK) || access("/sys/power/wake_unlock", W_OK))) {
        result = -errno;
        snprintf(reason, size, "Timed kernel wake locks are unavailable: %s", strerror(-result));
        return result;
    }
    if (access("/var/run", W_OK))
        return -errno;
    result = probe_power_lock(LOCK_SH, reason, size);
    if (result)
        return result;
    if (d->vendor == ER_KINDLE) {
        int battery;
        result = er_kindle_get("com.lab126.powerd", "battLevel", &battery);
        if (result) {
            snprintf(reason, size, "Kindle powerd is unavailable");
            return result;
        }
        snprintf(reason,
                 size,
                 "%s",
                 powerd_only_awake(d) ? "Powerd idle-timer renewal for the lease duration"
                                      : "Timed kernel lease and powerd idle-timer renewal");
    } else {
        snprintf(reason, size, "Timed kernel lease; Nickel sleep policy requires a native session");
    }
    return 0;
}

int er_renew_awake(er_device *d, unsigned seconds)
{
    if (!seconds || seconds > 86400)
        return -EINVAL;
    int result = er_supported(d, ER_AWAKE_LEASE);
    if (result)
        return result;
    int fd = lock_power(LOCK_SH, true);
    if (fd < 0)
        return fd;
    if (d->vendor == ER_KINDLE) {
        result = er_kindle_set("com.lab126.powerd", "touchScreenSaverTimeout", 1);
        if (result)
            goto done;
    }
    if (powerd_only_awake(d)) {
        result = renew_powerd_awake(d, seconds, fd);
        if (!result)
            return 0;
        goto done;
    }
    if (!d->awake_lock_name[0])
        snprintf(d->awake_lock_name,
                 sizeof(d->awake_lock_name),
                 "ereader-hal-awake-%ld-%p",
                 (long)getpid(),
                 (void *)d);
    char text[96];
    snprintf(text,
             sizeof(text),
             "%s %llu\n",
             d->awake_lock_name,
             (unsigned long long)seconds * 1000000000);
    int64_t until = er_now_ms() + (int64_t)seconds * 1000;
    result = er_write_text("/sys/power/wake_lock", text);
    if (!result && until > d->awake_until_ms)
        d->awake_until_ms = until;
done:
    close(fd);
    return result;
}

int er_release_awake(er_device *d)
{
    if (!d)
        return -EINVAL;
    if (d->powerd_awake) {
        struct er_powerd_awake *lease = d->powerd_awake;
        pthread_mutex_lock(&lease->mutex);
        lease->stop = true;
        pthread_cond_signal(&lease->changed);
        pthread_mutex_unlock(&lease->mutex);
        int result = pthread_join(lease->thread, NULL);
        if (result)
            return -result;
        pthread_cond_destroy(&lease->changed);
        pthread_mutex_destroy(&lease->mutex);
        free(lease);
        d->powerd_awake = NULL;
        d->awake_until_ms = 0;
        return 0;
    }
    if (!d->awake_until_ms)
        return 0;
    int result = er_write_text("/sys/power/wake_unlock", d->awake_lock_name);
    if (result == -EINVAL && d->awake_until_ms <= er_now_ms())
        result = 0;
    if (!result)
        d->awake_until_ms = 0;
    return result;
}
