#ifndef EREADER_HAL_H
#define EREADER_HAL_H
#include "ereader_hal_types.h"
#include "ereader_hal_usb.h"
#include "ereader_hal_wifi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum er_capability {
    ER_BRIGHTNESS = 1u << 0,
    ER_WARMTH = 1u << 1,
    ER_WIFI = 1u << 2,
    ER_BATTERY = 1u << 3,
    ER_DISPLAY = 1u << 4,
    ER_RTC_WAKE = 1u << 5,
    ER_SUSPEND = 1u << 6,
    ER_ROTATION = 1u << 7,
    ER_TOUCH = 1u << 8,
    ER_BUTTONS = 1u << 9,
    ER_COVER = 1u << 10,
    ER_ORIENTATION = 1u << 11,
    ER_POWER_EVENTS = 1u << 12,
    ER_USB = 1u << 13,
    ER_FRONTLIGHT = 1u << 14,
    ER_AUTO_BRIGHTNESS = 1u << 15,
    ER_INHIBIT_HAL = 1u << 16,
    ER_INHIBIT_SYSTEM = 1u << 17,
    ER_WIFI_STATUS = 1u << 18,
    ER_WIFI_SCAN = 1u << 19,
    ER_WIFI_PROFILES = 1u << 20,
    ER_WIFI_CONNECT = 1u << 21,
    ER_AWAKE_LEASE = 1u << 22,
    ER_BATTERY_DETAILS = 1u << 23,
};

#define ER_CAPABILITY_COUNT 24

/* Hardware/firmware facts, not a promise of an implemented HAL control. */
struct er_features {
    bool navigation_keys;
    bool dpad;
    bool firmware_keyboard;
    bool multitouch;
    bool light_sensor;
    bool natural_light_mixer;
    bool auxiliary_battery;
    bool charging_led;
    bool native_light_off;
};

struct er_info {
    const char *model;
    const char *codes;
    const char *codenames;
    uint32_t capabilities; /* Supported, not current availability. */
    unsigned panel_width;
    unsigned panel_height;
    unsigned dpi;
    bool color;
    bool touch;
    bool stylus;
    bool page_buttons;
    bool power_button;
    bool cover;
    bool orientation_sensor;
    const char *suspend_mode; /* "mem", "freeze", "none", or "unknown" for catalogue entries. */
    struct er_features features;
};

/* Runtime identity fields are empty when firmware does not expose them.
 * Reading identity never enables Wi-Fi. wifi_mac describes the current netdev. */
struct er_identity {
    char serial[64];
    char wifi_interface[16];
    char wifi_mac[18];
};

int er_get_identity(const er_device *device, struct er_identity *identity);

enum er_charge_state {
    ER_CHARGE_UNKNOWN,
    ER_CHARGING,
    ER_DISCHARGING,
    ER_FULL,
    ER_NOT_CHARGING,
};

struct er_battery {
    int percent;
    enum er_charge_state state;
};

enum er_battery_field {
    ER_BATTERY_VOLTAGE = 1u << 0,
    ER_BATTERY_CURRENT = 1u << 1,
    ER_BATTERY_TEMPERATURE = 1u << 2,
    ER_BATTERY_CHARGE = 1u << 3,
    ER_BATTERY_FULL = 1u << 4,
    ER_BATTERY_DESIGN = 1u << 5,
    ER_BATTERY_CYCLES = 1u << 6,
    ER_BATTERY_HEALTH = 1u << 7
};

struct er_battery_details {
    uint32_t present;
    int voltage_uv;
    int current_ua;
    int temperature_millicelsius;
    int charge_uah;
    int full_uah;
    int design_uah;
    int cycle_count;
    char health[32];
};

struct er_capability_change {
    enum er_capability capability;
    struct er_capability_info info;
};

struct er_display {
    unsigned width;
    unsigned height;
    unsigned bits_per_pixel;
    unsigned rotation; /* Degrees clockwise from upright portrait: 0,90,180,270. */
};

struct er_frontlight {
    bool enabled;
    int brightness;
    int warmth;             /* Actual percentage; -1 if unsupported. */
    int restore_brightness; /* Remembered by this handle only; -1 if unknown. */
};

enum er_usb_connection_state {
    ER_USB_DISCONNECTED,
    ER_USB_CONNECTED,
    ER_USB_CHARGING,
    ER_USB_DATA,
};

struct er_usb {
    enum er_usb_connection_state state;
    bool external_power;
    bool storage_mounted, storage_writable; /* Local mount state, not exclusive ownership. */
};

enum er_button {
    ER_BUTTON_PREVIOUS,
    ER_BUTTON_NEXT,
    ER_BUTTON_POWER,
    ER_BUTTON_HOME,
    ER_BUTTON_BACK,
    ER_BUTTON_MENU,
    ER_BUTTON_UP,
    ER_BUTTON_DOWN,
    ER_BUTTON_LEFT,
    ER_BUTTON_RIGHT,
    ER_BUTTON_SELECT,
    ER_BUTTON_KEYBOARD
};

enum er_action {
    ER_RELEASE,
    ER_PRESS,
    ER_REPEAT,
    ER_MOVE,
    ER_CANCEL,
};

enum er_wake_reason {
    ER_WAKE_UNKNOWN,
    ER_WAKE_RTC,
    ER_WAKE_POWER,
    ER_WAKE_COVER,
};

struct er_wake {
    enum er_wake_reason reason;
    int irq;
}; /* irq -1 when unknown. */
enum er_event_type {
    ER_EVENT_BUTTON,
    ER_EVENT_TOUCH,
    ER_EVENT_COVER,
    ER_EVENT_ORIENTATION,
    ER_EVENT_BATTERY,
    ER_EVENT_LOW_BATTERY,
    ER_EVENT_USB,
    ER_EVENT_PREPARE_SLEEP,
    ER_EVENT_RESUMED,
    ER_EVENT_SUSPEND_FAILED,
    ER_EVENT_INPUT_RESET,
    ER_EVENT_STATUS_ERROR,
    ER_EVENT_WIFI,
    ER_EVENT_CAPABILITY
};

struct er_event {
    enum er_event_type type;
    union {
        struct {
            enum er_button button;
            enum er_action action;
        } button;
        struct {
            int id, x, y;
            enum er_action action;
        } touch;
        bool cover_closed;
        unsigned orientation;
        struct er_battery battery;
        struct er_usb usb;
        struct er_wake wake;
        struct er_wifi_status wifi;
        struct er_capability_change capability;
        struct {
            enum er_capability capability;
            int error;
        } failure;
    } data;
};

/* Called synchronously around HAL-requested suspend, not vendor-daemon sleep.
 * PREPARE_SLEEP may return a negative errno to veto; other returns are ignored.
 * Do not call back into this device from the lifecycle callback.
 */
typedef int (*er_lifecycle_handler)(const struct er_event *, void *context);

/* Functions return 0 or negative errno unless documented otherwise. Serialize calls on each handle.
 */
/* Catalog listing does not open hardware. NULL marks the end. */
const char *er_model_name(unsigned index);
/* Static catalogue facts; panel dimensions are 0 and suspend mode is unknown. */
int er_get_model_info(unsigned index, struct er_info *info);
int er_open(er_device **device);
/* Cleanup failure leaves the handle open so the caller can retry. */
int er_close(er_device *device);
const struct er_info *er_get_info(const er_device *device);
const char *er_capability_name(enum er_capability capability);
int er_get_capability(er_device *device, enum er_capability capability,
                      struct er_capability_info *info);
/* An application-owned service may provide live state for a write-only light.
 * Both callbacks use percentages and return 0 or a negative errno. They must
 * remain valid until cleared or close, and must not call this device recursively.
 * Registration is per handle; NULL callbacks restore the native backend. */
typedef int (*er_brightness_reader)(int *percent, void *context);
typedef int (*er_brightness_writer)(int percent, void *context);
int er_set_brightness_provider(er_device *device, er_brightness_reader reader,
                               er_brightness_writer writer, void *context);
int er_get_brightness(const er_device *device, int *percent);
int er_set_brightness(er_device *device, int percent);
int er_get_warmth(const er_device *device, int *percent);
int er_set_warmth(er_device *device, int percent);
int er_get_frontlight(const er_device *device, struct er_frontlight *light);
int er_set_frontlight(er_device *device, bool enabled);
int er_set_auto_brightness(const er_device *device, bool enabled);
int er_get_battery(const er_device *device, struct er_battery *battery);
int er_get_battery_details(const er_device *device, struct er_battery_details *details);
int er_get_usb(const er_device *device, struct er_usb *usb);
int er_get_cover(er_device *device, bool *closed);
int er_get_display(const er_device *device, struct er_display *display);
/* Changes framebuffer rotation, not pixel contents. Redraw after success.
 * Emitted touch points use the resulting screen space.
 */
int er_set_rotation(er_device *device, unsigned degrees);
/* Returns 1 for an event, 0 for timeout, or negative errno. timeout_ms: -1 forever.
 * Events are observed without grabbing input. This only reads input and queued events;
 * call er_sample_status to collect firmware status and capability changes.
 * INPUT_RESET means discard all held inputs/contacts (queue loss or rotation).
 * Lifecycle notifications use the separate synchronous handler below.
 */
int er_poll_event(er_device *device, struct er_event *event, int timeout_ms);
/* Exclusively route navigation-bearing button devices to this handle. Touch and
 * power-only devices remain shared. Close/process exit releases all grabs. */
int er_grab_buttons(er_device *device);
int er_release_buttons(er_device *device);
/* Sample battery, USB, cover, watched capabilities and Wi-Fi into the event queue.
 * Firmware probes are synchronous. Call on a schedule appropriate to the application. */
int er_sample_status(er_device *device);
/* Watch selected capabilities during status sampling. Includes initial snapshots and changes.
 * Watching WIFI_STATUS also emits connection-status changes. Zero disables watches. */
int er_watch_capabilities(er_device *device, uint32_t capabilities);
int er_set_low_battery_threshold(er_device *device, int percent); /* Default 10. */
int er_set_lifecycle_handler(er_device *device, er_lifecycle_handler handler, void *context);
int er_get_wake_reason(const er_device *device, struct er_wake *wake);
/* Nested per handle. HAL scope blocks cooperating HAL suspend calls across
 * processes. SYSTEM scope additionally holds a kernel wake lock where the firmware exposes one.
 * All inhibitors must be released before close; close attempts cleanup too.
 */
int er_inhibit_suspend(er_device *device, bool system_scope);
int er_release_suspend(er_device *device);
/* Renewable timeout in seconds, 1..86400. Release ends this handle's lease early.
 * Kindles without kernel wake locks renew powerd's idle timer while active;
 * release restores normal idle behavior and does not override manual sleep. */
int er_renew_awake(er_device *device, unsigned seconds);
int er_release_awake(er_device *device);
/* Kindle display retention suppresses the stock screensaver without preventing
 * kernel suspend. One handle owns the system policy; release/close restores the
 * previous value. Keep this handle in a supervisor if the UI may be killed. */
int er_retain_display(er_device *device);
int er_release_display(er_device *device);
/* Elapsed kernel uptime in the last confirmed suspend cycle; 0 on a refused attempt. */
int er_get_last_sleep_ms(const er_device *device, uint64_t *milliseconds);
/* Portion of that cycle excluded from CLOCK_MONOTONIC. Add this, not the full
 * cycle duration, to application monotonic deadlines across suspend. */
int er_get_last_sleep_gap_ms(const er_device *device, uint64_t *milliseconds);
/* Last HAL suspend's wake source, including RTC child IRQs behind a PMIC parent. */
int er_get_last_wake(const er_device *device, struct er_wake *wake);
/* RTC-relative seconds, 1..ER_RTC_WAKE capability maximum. Existing alarms are
 * never replaced. Explicit
 * cancel cancels any owner's alarm. Timed suspend cleans its own alarm on return.
 * Another wake source can cause early resume.
 */
int er_set_wake_alarm(const er_device *device, unsigned seconds);
int er_cancel_wake_alarm(const er_device *device);
int er_suspend(er_device *device);
int er_suspend_for(er_device *device, unsigned seconds);
#ifdef __cplusplus
}
#endif
#endif
