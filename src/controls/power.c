#define _GNU_SOURCE
#include "controls/power.h"
#include "backends/kindle/lipc_api.h"
#include "backends/network.h"
#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#define ER_POWER_LOCK "/var/run/ereader-hal-suspend.lock"
#define ALARM "/sys/class/rtc/rtc0/wakealarm"

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
        if (result)
            return result;
    }
    FILE *sources = fopen("/sys/kernel/debug/wakeup_sources", "r");
    if (!sources) {
        snprintf(reason, size, "No HAL inhibitor; kernel wake-source reporting unavailable");
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), sources)) {
        char *columns = strchr(line, '\t');
        if (!columns)
            continue;
        *columns++ = '\0';
        unsigned long long count, events, wakeups, expires, active;
        if (sscanf(columns,
                   "%llu %llu %llu %llu %llu",
                   &count,
                   &events,
                   &wakeups,
                   &expires,
                   &active) != 5 ||
            !active)
            continue;
        size_t length = strlen(line);
        while (length && line[length - 1] == ' ')
            line[--length] = 0;
        snprintf(reason, size, "Active kernel wake source: %.90s", line);
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
    if (!d || !seconds || seconds > 86400)
        return -EINVAL;
    int result = er_supported(d, ER_RTC_WAKE);
    if (result)
        return result;
    char existing[64], text[32];
    result = er_read_text(ALARM, existing, sizeof(existing));
    if (result)
        return result;
    if (existing[0] && existing[0] != '\n')
        return -EBUSY;
    snprintf(text, sizeof(text), "+%u\n", seconds);
    return er_write_text(ALARM, text);
}

int er_cancel_wake_alarm(const er_device *d)
{
    int result = er_supported(d, ER_RTC_WAKE);
    return result ? result : er_write_text(ALARM, "0\n");
}

static int notify(er_device *d, enum er_event_type type, int error)
{
    if (!d->lifecycle)
        return 0;
    struct er_event event = {.type = type};
    if (type == ER_EVENT_RESUMED)
        er_get_wake_reason(d, &event.data.wake);
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
    char reason[128], armed[64] = {0};
    int lock = -1;
    bool did_suspend = false, alarm_set = false;
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
    result = notify(d, ER_EVENT_PREPARE_SLEEP, 0);
    if (result > 0)
        result = -EINVAL;
    if (result)
        goto done;
    if (seconds) {
        result = er_set_wake_alarm(d, seconds);
        if (result)
            goto done;
        alarm_set = true;
        result = er_read_text(ALARM, armed, sizeof(armed));
        if (result)
            goto done;
        char now[64], *end;
        result = er_read_text("/sys/class/rtc/rtc0/since_epoch", now, sizeof(now));
        if (result)
            goto done;
        long long deadline = strtoll(armed, &end, 10);
        if (end == armed || deadline <= 0) {
            result = -EIO;
            goto done;
        }
        long long rtc_now = strtoll(now, &end, 10);
        if (end == now || rtc_now <= 0) {
            result = -EIO;
            goto done;
        }
        if (deadline <= rtc_now) {
            result = -ETIMEDOUT;
            goto done;
        }
    }
    /* Wakeup-count reads block indefinitely with vendor USB wake locks. */
    result = er_write_text("/sys/power/state", d->info.suspend_mode);
    did_suspend = result == 0;
done:
    if (alarm_set) {
        char current[64];
        int cleanup = er_read_text(ALARM, current, sizeof(current));
        if (!cleanup && armed[0] && !strcmp(current, armed))
            cleanup = er_cancel_wake_alarm(d);
        else if (!cleanup && current[0] && current[0] != '\n')
            cleanup = -EBUSY;
        if (!result)
            result = cleanup;
    }
    if (lock >= 0)
        close(lock);
    if (did_suspend) {
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
    if (!seconds || seconds > 86400)
        return -EINVAL;
    return suspend_device(d, seconds);
}

int er_awake_probe(const er_device *d, char *reason, size_t size)
{
    int result = er_supported(d, ER_AWAKE_LEASE);
    if (result)
        return result;
    if (access("/sys/power/wake_lock", W_OK) || access("/sys/power/wake_unlock", W_OK)) {
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
        snprintf(reason, size, "Timed kernel lease and powerd idle-timer renewal");
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
    if (!d->awake_until_ms)
        return 0;
    int result = er_write_text("/sys/power/wake_unlock", d->awake_lock_name);
    if (result == -EINVAL && d->awake_until_ms <= er_now_ms())
        result = 0;
    if (!result)
        d->awake_until_ms = 0;
    return result;
}
