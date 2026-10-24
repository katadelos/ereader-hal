#define _GNU_SOURCE
#include "backends/kobo/setup.h"
#include "linux/io.h"
#include "profiles/profile.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const struct er_profile *profile_at(unsigned index)
{
    if (index < er_kindle_device_count)
        return &er_kindle_devices[index];

    index -= er_kindle_device_count;
    return index < er_kobo_device_count ? &er_kobo_devices[index] : NULL;
}

const char *er_model_name(unsigned index)
{
    const struct er_profile *profile = profile_at(index);
    return profile ? profile->name : NULL;
}

void er_profile_info(const struct er_profile *profile, struct er_info *info)
{
    *info = (struct er_info){
        .model = profile->name,
        .codes = profile->codes,
        .codenames = profile->codenames ? profile->codenames : "",
        .capabilities = profile->capabilities,
        .dpi = profile->dpi,
        .color = profile->color,
        .touch = (profile->capabilities & ER_TOUCH) != 0,
        .stylus = profile->stylus,
        .page_buttons = profile->page_buttons,
        .power_button = true,
        .cover = (profile->capabilities & ER_COVER) != 0,
        .orientation_sensor = (profile->capabilities & ER_ORIENTATION) != 0,
        .suspend_mode = "unknown",
        .features = profile->features,
    };
}

int er_get_model_info(unsigned index, struct er_info *info)
{
    if (!info)
        return -EINVAL;

    const struct er_profile *profile = profile_at(index);
    if (!profile)
        return -ENOENT;

    er_profile_info(profile, info);
    return 0;
}

bool er_profile_matches(const char *codes, const char *code)
{
    if (!codes || !code || !*code)
        return false;

    size_t length = strlen(code);
    while (*codes) {
        size_t count = strcspn(codes, " ");
        if (count == length && !strncmp(codes, code, count))
            return true;
        codes += count;
        if (*codes)
            ++codes;
    }
    return false;
}

static const struct er_profile *detect_kindle(const char *serial)
{
    bool old = serial[0] == 'B' || serial[0] == '9';
    unsigned offset = old ? 2 : 3;
    unsigned length = old ? 2 : 3;
    if (strlen(serial) < offset + length)
        return NULL;

    char code[4];
    memcpy(code, serial + offset, length);
    code[length] = 0;

    for (size_t i = 0; i < er_kindle_device_count; ++i) {
        if (er_profile_matches(er_kindle_devices[i].codes, code))
            return &er_kindle_devices[i];
    }
    return NULL;
}

static const struct er_profile *detect_kobo(void)
{
    const char *product = getenv("MODEL_NUMBER");
    const char *codename = getenv("PRODUCT");
    const char *platform = getenv("PLATFORM");
    char version[256];
    char code[4] = {0};

    if (product && *product) {
        if (strlen(product) != 3 || strspn(product, "0123456789") != 3)
            return NULL;
        memcpy(code, product, sizeof(code));
    } else if (!er_read_line("/mnt/onboard/.kobo/version", version, sizeof(version))) {
        size_t length = strlen(version);
        if (length < 3)
            return NULL;
        memcpy(code, version + length - 3, sizeof(code));
        if (strspn(code, "0123456789") != 3)
            return NULL;
    }

    /* Touch A and B share 310; the CPU/firmware platform identifies the board. */
    if (!strcmp(code, "310") && (!platform || !*platform)) {
        char cpu[64];
        if (er_kobo_hardware_value(code, "CPU", cpu, sizeof(cpu)))
            return NULL;
        if (!strcmp(cpu, "mx35"))
            platform = "freescale";
        else if (!strcmp(cpu, "mx50"))
            platform = "mx50-ntx";
        else
            return NULL;
    }

    const struct er_profile *match = NULL;
    for (size_t i = 0; i < er_kobo_device_count; ++i) {
        const struct er_profile *profile = &er_kobo_devices[i];
        if (code[0]) {
            if (!er_profile_matches(profile->codes, code))
                continue;
        } else if (!er_profile_matches(profile->codenames, codename)) {
            continue;
        }
        if (profile->platform && (!platform || strcmp(profile->platform, platform)))
            continue;
        /* Codenames shared by hardware revisions need a product ID. */
        if (match)
            return NULL;
        match = profile;
    }
    return match;
}

const struct er_profile *er_detect_profile(void)
{
    char serial[256];
    if (!er_read_line("/proc/usid", serial, sizeof(serial)))
        return detect_kindle(serial);

    return detect_kobo();
}
