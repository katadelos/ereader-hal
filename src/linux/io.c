#define _GNU_SOURCE
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int er_read_text(const char *path, char *text, size_t size)
{
    if (size < 2)
        return -EINVAL;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    ssize_t n;
    do {
        n = read(fd, text, size - 1);
    } while (n < 0 && errno == EINTR);
    int result = n < 0 ? -errno : 0;
    close(fd);
    if (result)
        return result;
    text[n] = '\0';
    return 0;
}

int er_read_line(const char *path, char *text, size_t size)
{
    int result = er_read_text(path, text, size);
    if (!result)
        text[strcspn(text, "\r\n")] = 0;
    return result;
}

int er_parse_int(const char *text, int *value)
{
    char *end;
    errno = 0;
    long long n = strtoll(text, &end, 10);
    if (end == text || errno || n < INT_MIN || n > INT_MAX)
        return -EIO;
    while (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')
        ++end;
    if (*end)
        return -EIO;
    *value = (int)n;
    return 0;
}

int er_read_int(const char *path, int *value)
{
    char text[64];
    int result = er_read_text(path, text, sizeof(text));
    return result ? result : er_parse_int(text, value);
}

int er_write_bytes(const char *path, const void *data, size_t size)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    ssize_t n;
    do {
        n = write(fd, data, size);
    } while (n < 0 && errno == EINTR);
    int result = n < 0 ? -errno : ((size_t)n == size ? 0 : -EIO);
    if (close(fd) < 0 && !result)
        result = -errno;
    return result;
}

int er_write_text(const char *path, const char *text)
{
    return er_write_bytes(path, text, strlen(text));
}

int er_write_int(const char *path, int value)
{
    char text[32];
    snprintf(text, sizeof(text), "%d\n", value);
    return er_write_text(path, text);
}

int64_t er_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Lab126 exposes its RTC-measured suspend total as uptime's fourth field.
 * Prefer that explicit counter: older kernels can overflow the 32-bit
 * nanosecond sum in CLOCK_BOOTTIME before widening it. Other kernels use
 * CLOCK_BOOTTIME (Linux UAPI 7, absent from early Kindle headers), or their
 * RTC directly in the suspend transaction when that clock is unavailable. */
int er_boot_ms(int64_t *milliseconds)
{
    char text[128];
    int result = er_read_text("/proc/uptime", text, sizeof(text));
    unsigned long long slept;
    char extra;
    if (!result && sscanf(text, "%*s %*s %*s %llu %c", &slept, &extra) == 1) {
        int64_t awake = er_now_ms();
        if (slept > (unsigned long long)(INT64_MAX - awake) / 1000)
            return -EOVERFLOW;
        *milliseconds = awake + (int64_t)slept * 1000;
        return 0;
    }
    struct timespec now;
    if (!clock_gettime((clockid_t)7, &now)) {
        *milliseconds = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        return 0;
    }
    return errno == EINVAL ? (result ? result : -EOPNOTSUPP) : -errno;
}

int er_sleep_ms(unsigned milliseconds)
{
    struct timespec remaining = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    while (nanosleep(&remaining, &remaining) < 0)
        if (errno != EINTR)
            return -errno;
    return 0;
}
