#ifndef ER_PROFILES_H
#define ER_PROFILES_H

#include "ereader_hal.h"
#include <stddef.h>

#define COMMON_CAPS                                                                                \
    (ER_WIFI | ER_BATTERY | ER_DISPLAY | ER_RTC_WAKE | ER_SUSPEND | ER_ROTATION | ER_BUTTONS |     \
     ER_POWER_EVENTS | ER_USB | ER_INHIBIT_HAL | ER_WIFI_STATUS | ER_WIFI_SCAN |                   \
     ER_WIFI_PROFILES | ER_WIFI_CONNECT | ER_AWAKE_LEASE | ER_BATTERY_DETAILS)

enum er_vendor {
    ER_KOBO,
    ER_KINDLE,
};

enum er_display_driver {
    ER_EINKFB,
    ER_KINDLE_PEARL,
    ER_KINDLE_CARTA,
    ER_KINDLE_ZELDA,
    ER_KINDLE_REX,
    ER_KINDLE_MTK,
    ER_KOBO_NTX,
    ER_KOBO_V2,
    ER_KOBO_MTK,
    ER_KOBO_SUNXI,
    ER_DISPLAY_UNKNOWN
};

enum er_rotation_quirk {
    ER_ROTA_NORMAL,
    ER_ROTA_INVERT_ODD,
    ER_ROTA_INVERT_ALL,
};

struct er_profile {
    const char *name;
    const char *codes;
    const char *codenames;
    const char *platform;
    enum er_vendor vendor;
    enum er_display_driver display_driver;
    uint32_t capabilities;
    unsigned dpi;
    struct er_features features;
    bool page_buttons;
    bool stylus;
    bool color;
    bool light_off;
    bool swap_axes;
    bool mirror_x;
    bool mirror_y;
    unsigned rotation_map[4]; /* Native -> canonical quarter turns. */
    enum er_rotation_quirk rotation_quirk;
    const char *battery_capacity, *battery_status, *charging;
    const char *warmth_path, *light_off_path;
    const char *light_channels[3]; /* White, red, optional green. */
    bool warmth_inverted;
    bool usb_fixed_hid;
    bool usb_single_gadget;
};

void er_profile_info(const struct er_profile *profile, struct er_info *info);
const struct er_profile *er_detect_profile(void);
bool er_profile_matches(const char *codes, const char *code);

extern const struct er_profile er_kindle_devices[];
extern const size_t er_kindle_device_count;
extern const struct er_profile er_kobo_devices[];
extern const size_t er_kobo_device_count;

#endif
