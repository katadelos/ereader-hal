#ifndef ER_DEVICE_H
#define ER_DEVICE_H
#include "backends/kobo/setup.h"
#include "linux/evdev.h"
#include "profiles/profile.h"

#define ER_QUEUE_SIZE 64

struct er_device {
    struct er_usb_session *usb_session;
    struct er_usb_config *usb_config;
    enum er_vendor vendor;
    const struct er_profile *profile;
    uint32_t implemented;
    char battery_capacity[256], battery_status[256];
    char warmth_path[256];
    char wifi_path[256], wifi_power_path[256];
    char wifi_dependency_path[256];
    enum er_wifi_power wifi_power;
    char wifi_module[32];
    char wifi_interface[16];
    struct er_info info;
    int brightness_max, saved_brightness;
    bool light_valid;
    int light_brightness, light_warmth, light_values[3];
    struct er_input input[ER_INPUT_COUNT];
    struct er_event queue[ER_QUEUE_SIZE];
    unsigned queue_head, queue_count;
    bool polling_started, battery_valid, usb_valid, cover_valid;
    struct er_battery last_battery;
    struct er_usb last_usb;
    bool last_cover;
    uint32_t watched_capabilities, capability_valid;
    struct er_capability_info last_capability[ER_CAPABILITY_COUNT];
    bool wifi_valid;
    struct er_wifi_status last_wifi;
    int low_battery_threshold;
    int64_t sleep_delta_ms;
    unsigned input_rotation;
    int inhibit_fd;
    unsigned inhibit_count;
    bool system_inhibit;
    char wake_lock_name[64];
    char awake_lock_name[64];
    int64_t awake_until_ms;
    er_lifecycle_handler lifecycle;
    void *lifecycle_context;
};
int er_supported(const er_device *device, enum er_capability capability);
#endif
