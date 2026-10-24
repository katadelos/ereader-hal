#include "profiles/profile.h"

/* Model IDs and hardware facts: KOReader frontend/device/kindle/device.lua,
 * revision afff28b107a7ea4ab5867b5e65dd054987ca1d99. Rotation maps: FBInk. */
#define KINDLE_CAPS (COMMON_CAPS | ER_INHIBIT_SYSTEM)
#define KINDLE_TOUCH_CAPS (KINDLE_CAPS | ER_TOUCH)
#define KINDLE_LIGHT_CAPS (KINDLE_TOUCH_CAPS | ER_BRIGHTNESS | ER_FRONTLIGHT)

const struct er_profile er_kindle_devices[] = {
    {
        .name = "Kindle 4 / 5",
        .codes = "0E 23",
        .vendor = ER_KINDLE,
        .display_driver = ER_EINKFB,

        .capabilities = KINDLE_CAPS,
        .dpi = 167,
        .page_buttons = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.dpad = true,
        .features.firmware_keyboard = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/yoshi_battery/yoshi_battery0/battery_capacity",
        .charging = "/sys/devices/platform/fsl-usb2-udc/charging",
    },

    {
        .name = "Kindle Touch",
        .codes = "0F 11 10 12",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_PEARL,

        .capabilities = KINDLE_TOUCH_CAPS | ER_COVER,
        .dpi = 167,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/yoshi_battery/yoshi_battery0/battery_capacity",
        .charging = "/sys/devices/platform/fsl-usb2-udc/charging",
    },

    {
        .name = "Kindle Paperwhite",
        .codes = "24 1B 1D 1F 1C 20",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_PEARL,

        .capabilities = KINDLE_LIGHT_CAPS | ER_COVER,
        .dpi = 212,
        .features.multitouch = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/yoshi_battery/yoshi_battery0/battery_capacity",
        .charging = "/sys/devices/platform/aplite_charger.0/charging",

        .light_off_path = "/sys/devices/system/fl_tps6116x/fl_tps6116x0/fl_intensity",
    },

    {
        .name = "Kindle Paperwhite 2",
        .codes = "D4 5A D5 D6 D7 D8 F2 17 60 F4 F9 62 61 5F",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_CARTA,

        .capabilities = KINDLE_LIGHT_CAPS | ER_COVER,
        .dpi = 212,
        .features.multitouch = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/wario_battery/wario_battery0/battery_capacity",
        .battery_status = "/sys/class/power_supply/max77696-battery/status",
        .charging = "/sys/devices/system/wario_charger/wario_charger0/charging",

        .light_off_path = "/sys/class/backlight/max77696-bl/brightness",
    },

    {
        .name = "Kindle Basic 2014",
        .codes = "C6 DD",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_CARTA,

        .capabilities = KINDLE_TOUCH_CAPS | ER_COVER,
        .dpi = 167,
        .light_off = true,
        .features.multitouch = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/wario_battery/wario_battery0/battery_capacity",
        .charging = "/sys/devices/system/wario_charger/wario_charger0/charging",
    },

    {
        .name = "Kindle Voyage",
        .codes = "13 54 2A 4F 52 53",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_CARTA,

        .capabilities = KINDLE_LIGHT_CAPS | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .page_buttons = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.light_sensor = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/wario_battery/wario_battery0/battery_capacity",
        .charging = "/sys/devices/system/wario_charger/wario_charger0/charging",

        .light_off_path = "/sys/class/backlight/max77696-bl/brightness",
    },

    {
        .name = "Kindle Paperwhite 3",
        .codes = "0G1 0G2 0G4 0G5 0G6 0G7 0KB 0KC 0KD 0KE 0KF 0KG 0LK 0LL",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_CARTA,

        .capabilities = KINDLE_LIGHT_CAPS | ER_COVER,
        .dpi = 300,
        .features.multitouch = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/wario_battery/wario_battery0/battery_capacity",
        .charging = "/sys/devices/system/wario_charger/wario_charger0/charging",

        .light_off_path = "/sys/class/backlight/max77696-bl/brightness",
    },

    {
        .name = "Kindle Oasis",
        .codes = "0GC 0GD 0GR 0GS 0GT 0GU",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_CARTA,

        .capabilities = KINDLE_LIGHT_CAPS | ER_ORIENTATION | ER_COVER,
        .dpi = 300,
        .page_buttons = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.auxiliary_battery = true,
        .features.native_light_off = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/devices/system/wario_battery/wario_battery0/battery_capacity",
        .charging = "/sys/devices/system/wario_charger/wario_charger0/charging",
    },

    {
        .name = "Kindle Oasis 2",
        .codes = "0LM 0LN 0LP 0LQ 0P1 0P2 0P6 0P7 0P8 0S1 0S2 0S3 0S4 0S7 0SA",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_ZELDA,

        .capabilities = KINDLE_LIGHT_CAPS | ER_ORIENTATION | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .page_buttons = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.native_light_off = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/max77796-battery/capacity",
        .battery_status = "/sys/class/power_supply/max77796-charger/status",
        .charging = "/sys/class/power_supply/max77796-charger/charging",
    },

    {
        .name = "Kindle Oasis 3",
        .codes = "11L 0WQ 0WP 0WN 0WM 0WL",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_ZELDA,

        .capabilities =
            KINDLE_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .page_buttons = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/max77796-battery/capacity",
        .battery_status = "/sys/class/power_supply/max77796-charger/status",
        .charging = "/sys/class/power_supply/max77796-charger/charging",
    },

    {
        .name = "Kindle Basic 2016",
        .codes = "0DU 0K9 0KA",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_CARTA,

        .capabilities = KINDLE_TOUCH_CAPS | ER_COVER,
        .dpi = 167,
        .light_off = true,
        .features.multitouch = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/bd7181x_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd7181x_bat/status",
        .charging = "/sys/class/power_supply/bd7181x_bat/charging",
    },

    {
        .name = "Kindle Paperwhite 4",
        .codes =
            "0PP 0T1 0T2 0T3 0T4 0T5 0T6 0T7 0TJ 0TK 0TL 0TM 0TN 102 103 16Q 16R 16S 16T 16U 16V",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_REX,

        .capabilities = KINDLE_LIGHT_CAPS | ER_COVER,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Basic 2019",
        .codes = "10L 0WF 0WG 0WH 0WJ 0VB",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_REX,

        .capabilities = KINDLE_LIGHT_CAPS | ER_COVER,
        .dpi = 167,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Paperwhite 5",
        .codes = "1Q0 1PX 1VD 21A 2BJ 2DK",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities = KINDLE_LIGHT_CAPS | ER_WARMTH | ER_COVER,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",

        .usb_fixed_hid = true,
    },

    {
        .name = "Kindle Paperwhite 5 Signature",
        .codes = "1LG 219 2BH",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities = KINDLE_LIGHT_CAPS | ER_WARMTH | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",

        .usb_fixed_hid = true,
    },

    {
        .name = "Kindle Basic 2022",
        .codes = "22D 25T 23A 2AQ 2AP 1XH 22C",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities = KINDLE_LIGHT_CAPS | ER_COVER,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Scribe",
        .codes = "27J 2BL 263 227 2BM 23L 23M 270",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities =
            KINDLE_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .stylus = true,
        .light_off = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Scribe 2024",
        .codes = "3V0 3V1 3X5 3UV 3X4 3X3 41E 410",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities =
            KINDLE_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .stylus = true,
        .light_off = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Colorsoft",
        .codes = "3H2 3H4 3H6 3H7 3H9 3JT 3J6 455 456 4EP 34X 3HB",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities = KINDLE_LIGHT_CAPS | ER_WARMTH | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .color = true,
        .light_off = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Basic 2024 (KT6)",
        .codes = "A89 3L2 3L3 3L4 3L5 3L6 3KM",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities = KINDLE_LIGHT_CAPS | ER_COVER,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",

        .usb_fixed_hid = true,
    },

    {
        .name = "Kindle Paperwhite 6",
        .codes = "33W 33X 346 349 3H3 3H5 3H8 3HA 3J5 3JS",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities = KINDLE_LIGHT_CAPS | ER_WARMTH | ER_COVER,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Scribe 3",
        .codes = "4PG 4PE 4PL 4F8 4FA 454",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities =
            KINDLE_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .stylus = true,
        .light_off = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },

    {
        .name = "Kindle Scribe Colorsoft",
        .codes = "4VX 4PF 4PH 4F9 4FB 46P",
        .vendor = ER_KINDLE,
        .display_driver = ER_KINDLE_MTK,

        .capabilities =
            KINDLE_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION | ER_AUTO_BRIGHTNESS | ER_COVER,
        .dpi = 300,
        .stylus = true,
        .color = true,
        .light_off = true,
        .features.multitouch = true,
        .features.light_sensor = true,
        .features.natural_light_mixer = true,
        .features.native_light_off = true,

        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",
        .charging = "/sys/class/power_supply/bd71827_bat/charging",
    },
};

const size_t er_kindle_device_count = sizeof(er_kindle_devices) / sizeof(er_kindle_devices[0]);
