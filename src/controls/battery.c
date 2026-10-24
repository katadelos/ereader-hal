#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <string.h>
#define _GNU_SOURCE
#include <glob.h>
#include <limits.h>
#include <stdio.h>

int er_get_battery(const er_device *d, struct er_battery *battery)
{
    if (!battery)
        return -EINVAL;
    int result = er_supported(d, ER_BATTERY);
    if (result)
        return result;
    struct er_battery value = {0, ER_CHARGE_UNKNOWN};
    char text[64];
    result = er_read_int(d->battery_capacity, &value.percent);
    if (result)
        return result;
    if (value.percent < 0 || value.percent > 100)
        return -EIO;
    if (!d->battery_status[0]) {
        int charging;
        if (!d->profile->charging)
            return -ENOSYS;
        result = er_read_int(d->profile->charging, &charging);
        if (result)
            return result;
        value.state = charging ? ER_CHARGING : ER_DISCHARGING;
        *battery = value;
        return 0;
    }
    result = er_read_line(d->battery_status, text, sizeof(text));
    if (result)
        return result;
    if (!strcmp(text, "Charging"))
        value.state = ER_CHARGING;
    else if (!strcmp(text, "Discharging"))
        value.state = ER_DISCHARGING;
    else if (!strcmp(text, "Full"))
        value.state = ER_FULL;
    else if (!strcmp(text, "Not charging"))
        value.state = ER_NOT_CHARGING;
    *battery = value;
    return 0;
}

static int battery_directory(const er_device *d, char *directory, size_t size)
{
    glob_t paths = {0};
    int found = 0, result = -ENODEV;
    if (glob("/sys/class/power_supply/*/type", 0, NULL, &paths)) {
        globfree(&paths);
        return result;
    }
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        char type[32];
        if (er_read_text(paths.gl_pathv[i], type, sizeof(type)) || strncmp(type, "Battery", 7))
            continue;
        size_t length = strlen(paths.gl_pathv[i]) - strlen("/type");
        if (length >= size) {
            result = -ENAMETOOLONG;
            break;
        }
        memcpy(directory, paths.gl_pathv[i], length);
        directory[length] = 0;
        if (strlen(d->battery_capacity) == length + strlen("/capacity") &&
            !strncmp(d->battery_capacity, directory, length) &&
            !strcmp(d->battery_capacity + length, "/capacity")) {
            found = 1;
            result = 0;
            break;
        }
        ++found;
        result = 0;
    }
    globfree(&paths);
    return found > 1 ? -ENOTUNIQ : result;
}

int er_get_battery_details(const er_device *d, struct er_battery_details *details)
{
    if (!details)
        return -EINVAL;
    int result = er_supported(d, ER_BATTERY_DETAILS);
    if (result)
        return result;
    *details = (struct er_battery_details){0};
    char directory[256], path[320];
    result = battery_directory(d, directory, sizeof(directory));
    if (result)
        return result;
    struct {
        const char *name;
        enum er_battery_field flag;
        int *value;
    } fields[] = {
        {"voltage_now", ER_BATTERY_VOLTAGE, &details->voltage_uv},
        {"current_now", ER_BATTERY_CURRENT, &details->current_ua},
        {"temp", ER_BATTERY_TEMPERATURE, &details->temperature_millicelsius},
        {"charge_now", ER_BATTERY_CHARGE, &details->charge_uah},
        {"charge_full", ER_BATTERY_FULL, &details->full_uah},
        {"charge_full_design", ER_BATTERY_DESIGN, &details->design_uah},
        {"cycle_count", ER_BATTERY_CYCLES, &details->cycle_count},
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", directory, fields[i].name);
        result = er_read_int(path, fields[i].value);
        if (result == -ENOENT || result == -ENODATA || result == -ENODEV)
            continue;
        if (result)
            return result;
        if (fields[i].flag == ER_BATTERY_TEMPERATURE) {
            if (*fields[i].value > INT_MAX / 100 || *fields[i].value < INT_MIN / 100)
                return -ERANGE;
            *fields[i].value *= 100;
        }
        details->present |= fields[i].flag;
    }
    snprintf(path, sizeof(path), "%s/health", directory);
    result = er_read_line(path, details->health, sizeof(details->health));
    if (!result) {
        details->present |= ER_BATTERY_HEALTH;
    } else if (result != -ENOENT && result != -ENODATA && result != -ENODEV)
        return result;
    return details->present ? 0 : -ENODATA;
}
