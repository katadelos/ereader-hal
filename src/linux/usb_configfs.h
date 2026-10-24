#ifndef ER_USB_CONFIGFS_H
#define ER_USB_CONFIGFS_H
#include "backends/kindle/usb.h"

#define ER_USB_FUNCTIONS ((1u << 6) - 1)

struct er_usb_session {
    int lock;
    uint32_t functions, requested;
    char controller[64];
    bool created, prepared, bound, mounted_configfs, mounted_adb, created_adb;
    bool cleanup_pending, transition, restoring, previous_bound, reenumerated;
    int transition_error;
    struct er_usb_config *config, *previous;
    bool stock_network, previous_native;
    struct er_kindle_usb stock;
};

int er_usb_disconnect_controller(const char *controller);
int er_usb_validate_config(const er_device *d, const struct er_usb_config *config);
int er_usb_new_session(er_device *device);
int er_usb_prepare_session(er_device *device, const struct er_usb_config *config);
int er_usb_clear_session(er_device *device);
struct er_usb_config *er_usb_copy_config(const struct er_usb_config *config);
#endif
