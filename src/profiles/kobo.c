#include "profiles/profile.h"

/* Model IDs and hardware facts: KOReader frontend/device/kobo/device.lua,
 * revision afff28b107a7ea4ab5867b5e65dd054987ca1d99. Rotation maps: FBInk. */
#define KOBO_CAPS (COMMON_CAPS | ER_TOUCH | ER_COVER)
#define KOBO_LIGHT_CAPS (KOBO_CAPS | ER_BRIGHTNESS | ER_FRONTLIGHT)

const struct er_profile er_kobo_devices[] = {
    {
        .name = "Kobo Touch A",
        .codes = "310",
        .codenames = "trilogy",
        .platform = "freescale",
        .vendor = ER_KOBO,
        .display_driver = ER_DISPLAY_UNKNOWN,

        .capabilities = KOBO_CAPS,
        .dpi = 167,
        .light_off = true,
        .features.navigation_keys = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Touch B",
        .codes = "310",
        .codenames = "trilogy",
        .platform = "mx50-ntx",
        .vendor = ER_KOBO,
        .display_driver = ER_DISPLAY_UNKNOWN,

        .capabilities = KOBO_CAPS,
        .dpi = 167,
        .light_off = true,
        .features.navigation_keys = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Touch C",
        .codes = "320",
        .codenames = "trilogy",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_CAPS,
        .dpi = 167,
        .light_off = true,
        .features.navigation_keys = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Mini",
        .codes = "340",
        .codenames = "pixie",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_CAPS,
        .dpi = 200,
        .light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Glo",
        .codes = "330",
        .codenames = "kraken",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 212,
        .light_off = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {3, 2, 1, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura HD",
        .codes = "350",
        .codenames = "dragon",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 265,
        .light_off = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 0, 3, 2},
        .rotation_quirk = ER_ROTA_INVERT_ALL,

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura",
        .codes = "360",
        .codenames = "phoenix",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 212,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura H2O",
        .codes = "370",
        .codenames = "dahlia",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 265,
        .light_off = true,
        .features.multitouch = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 0, 3, 2},
        .rotation_quirk = ER_ROTA_INVERT_ALL,

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Glo HD",
        .codes = "371",
        .codenames = "alyssum",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Touch 2.0",
        .codes = "372",
        .codenames = "pika",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_CAPS,
        .dpi = 167,
        .light_off = true,
        .features.multitouch = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura One",
        .codes = "373 381",
        .codenames = "daylight",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .light_channels = {"/sys/class/backlight/lm3630a_led1b",
                           "/sys/class/backlight/lm3630a_led1a",
                           "/sys/class/backlight/lm3630a_ledb"},
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura H2O Edition 2",
        .codes = "374",
        .codenames = "snow",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH,
        .dpi = 265,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .rotation_map = {3, 0, 1, 2},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .light_channels = {"/sys/class/backlight/lm3630a_ledb",
                           "/sys/class/backlight/lm3630a_led",
                           "/sys/class/backlight/lm3630a_leda"},
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura H2O Edition 2 Rev 2",
        .codes = "378",
        .codenames = "snow",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH,
        .dpi = 265,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {3, 0, 1, 2},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .light_channels = {"/sys/class/backlight/lm3630a_ledb",
                           "/sys/class/backlight/lm3630a_leda"},
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura Edition 2",
        .codes = "375",
        .codenames = "star",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_NTX,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 212,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Aura Edition 2 Rev 2",
        .codes = "379",
        .codenames = "star",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 212,
        .light_off = true,
        .features.multitouch = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Clara HD",
        .codes = "376",
        .codenames = "nova",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_path = "/sys/class/backlight/lm3630a_led/color",
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Forma",
        .codes = "377 380",
        .codenames = "frost",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION,
        .dpi = 300,
        .page_buttons = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {3, 2, 1, 0},
        .rotation_quirk = ER_ROTA_INVERT_ODD,

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_path = "/sys/class/backlight/tlc5947_bl/color",
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Libra H2O",
        .codes = "384",
        .codenames = "storm",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION,
        .dpi = 300,
        .page_buttons = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_path = "/sys/class/backlight/lm3630a_led/color",
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Nia",
        .codes = "382",
        .codenames = "luna",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS,
        .dpi = 212,
        .light_off = true,
        .features.multitouch = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {1, 2, 3, 0},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Elipsa",
        .codes = "387",
        .codenames = "europa",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_SUNXI,

        .capabilities = KOBO_LIGHT_CAPS | ER_ORIENTATION,
        .dpi = 227,
        .stylus = true,
        .light_off = true,
        .features.multitouch = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/battery/capacity",
        .battery_status = "/sys/class/power_supply/battery/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Sage",
        .codes = "383",
        .codenames = "cadmus",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_SUNXI,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION,
        .dpi = 300,
        .page_buttons = true,
        .stylus = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.auxiliary_battery = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {0, 1, 2, 3},

        .battery_capacity = "/sys/class/power_supply/battery/capacity",
        .battery_status = "/sys/class/power_supply/battery/status",

        .warmth_path = "/sys/class/leds/aw99703-bl_FL1/color",
    },

    {
        .name = "Kobo Libra 2",
        .codes = "388",
        .codenames = "io",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION,
        .dpi = 300,
        .page_buttons = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .rotation_map = {1, 0, 3, 2},

        .battery_capacity = "/sys/class/power_supply/mc13892_bat/capacity",
        .battery_status = "/sys/class/power_supply/mc13892_bat/status",

        .warmth_inverted = true,
    },

    {
        .name = "Kobo Clara 2E",
        .codes = "386",
        .codenames = "goldfinch",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_V2,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {3, 2, 1, 0},

        .battery_capacity = "/sys/class/power_supply/battery/capacity",
        .battery_status = "/sys/class/power_supply/battery/status",

        .warmth_path = "/sys/class/leds/aw99703-bl_FL1/color",
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Elipsa 2E",
        .codes = "389",
        .codenames = "condor",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_MTK,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION,
        .dpi = 227,
        .stylus = true,
        .light_off = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_y = true,
        .rotation_map = {1, 0, 3, 2},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",

        .warmth_path = "/sys/class/leds/aw99703-bl_FL1/color",
        .warmth_inverted = true,

        .usb_fixed_hid = true,
        .usb_single_gadget = true,
    },

    {
        .name = "Kobo Libra Colour",
        .codes = "390",
        .codenames = "monza monzaKobo monzaTolino",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_MTK,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH | ER_ORIENTATION,
        .dpi = 300,
        .page_buttons = true,
        .stylus = true,
        .color = true,
        .light_off = true,
        .features.navigation_keys = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_y = true,
        .rotation_map = {1, 0, 3, 2},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",

        .warmth_path = "/sys/class/backlight/lm3630a_led/color",
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Clara BW",
        .codes = "391",
        .codenames = "spaBW spaKoboBW spaTolinoBW spaBWTPV",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_MTK,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH,
        .dpi = 300,
        .light_off = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {3, 2, 1, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",

        .warmth_path = "/sys/class/backlight/lm3630a_led/color",
        .warmth_inverted = true,
    },

    {
        .name = "Kobo Clara Colour",
        .codes = "393",
        .codenames = "spaColour spaKoboColour spaTolinoColour",
        .vendor = ER_KOBO,
        .display_driver = ER_KOBO_MTK,

        .capabilities = KOBO_LIGHT_CAPS | ER_WARMTH,
        .dpi = 300,
        .color = true,
        .light_off = true,
        .features.multitouch = true,
        .features.natural_light_mixer = true,
        .features.charging_led = true,
        .features.native_light_off = true,

        .swap_axes = true,
        .mirror_x = true,
        .rotation_map = {3, 2, 1, 0},

        .battery_capacity = "/sys/class/power_supply/bd71827_bat/capacity",
        .battery_status = "/sys/class/power_supply/bd71827_bat/status",

        .warmth_path = "/sys/class/backlight/lm3630a_led/color",
        .warmth_inverted = true,
    },
};

const size_t er_kobo_device_count = sizeof(er_kobo_devices) / sizeof(er_kobo_devices[0]);
