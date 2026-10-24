#ifndef EREADER_HAL_USB_H
#define EREADER_HAL_USB_H
#include "ereader_hal_types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum er_usb_function {
    ER_USB_STORAGE = 1u << 0, /* Observed firmware storage; cannot be configured. */
    ER_USB_ECM = 1u << 1,
    ER_USB_RNDIS = 1u << 2,
    ER_USB_ADB = 1u << 3,
    ER_USB_HID = 1u << 4,
    ER_USB_SERIAL = 1u << 5
};

enum er_usb_mode {
    ER_USB_MODE_FILESTORAGE = 1u << 0, /* Status only; storage belongs to firmware. */
    ER_USB_MODE_NETWORK = 1u << 1,
    ER_USB_MODE_ADB = 1u << 2,
    ER_USB_MODE_HID = 1u << 3,
    ER_USB_MODE_SERIAL = 1u << 4
};

enum er_usb_mode_state { ER_USB_MODE_ACTIVE, ER_USB_MODE_PENDING, ER_USB_MODE_BLOCKED };

struct er_usb_gadget {
    uint32_t functions;
    bool prepared, bound, owned;
    char controller[64], gadget[256], state[32];
    char network_interface[16], adb_path[128];
};

struct er_usb_config {
    uint32_t functions;
    uint16_t vendor_id, product_id;
    const char *manufacturer, *product, *serial;
    const char *device_mac, *host_mac;
    const uint8_t *hid_descriptor;
    size_t hid_descriptor_size;
    unsigned hid_report_length, hid_protocol, hid_subclass;
    bool take_stock_usb; /* Kindle: coordinate volumd and restore stock USB on release. */
};

struct er_usb_transition {
    uint32_t requested, active;
    bool prepared, reenumerated, restored, cleanup_pending;
    int error, cleanup_error, restore_error; /* Positive errno values. */
};

struct er_usb_mode_transition {
    uint32_t requested, active; /* er_usb_mode flags, not er_usb_function flags. */
    bool prepared, reenumerated, restored, cleanup_pending;
    int error, cleanup_error, restore_error;
    enum er_usb_mode_state state;
};

/* Save a gadget template for subsequent mode switches. functions selects ECM/RNDIS for NETWORK
 * (default ECM); the other functions are selected by set_usb_mode. NULL selects stock Kindle
 * NETWORK handling. This copies the settings and does not change hardware.
 */
int er_configure_usb(er_device *device, const struct er_usb_config *config);
int er_get_usb_mode(const er_device *device, uint32_t *mode);
/* Claim a mode until release/close. Composite modes are ORed. FILESTORAGE is status-only.
 * Native Kindle NETWORK needs no template; custom gadgets require configure_usb first.
 * ADB returns PENDING until adbd opens its endpoints; call finish_usb_mode afterward.
 * Stop adbd after deactivate_usb before switching away from ADB.
 * Release restores the previous firmware policy; normal USB events select stock storage.
 */
int er_set_usb_mode(er_device *device, uint32_t mode, struct er_usb_mode_transition *transition);
int er_finish_usb_mode(er_device *device, struct er_usb_mode_transition *transition);

int er_get_usb_gadget(const er_device *device, struct er_usb_gadget *status);
int er_get_usb_function(const er_device *device, enum er_usb_function function,
                        struct er_capability_info *info);
/* Reports the selected functions together. The controller checks endpoint capacity at activation.
 */
int er_get_usb_combination(const er_device *device, uint32_t functions,
                           struct er_capability_info *info);
/* Apply replaces the owned configuration. NULL releases it. Replacing a bound configuration
 * disconnects USB. ADB may leave a prepared configuration; start adbd and call finish.
 * For active ADB, deactivate USB and stop adbd before replacing or releasing the configuration.
 * A failed transition restores the previous configuration when possible. If cleanup or restoration
 * fails, the handle retains the state needed by finish or release to retry.
 */
int er_apply_usb_config(er_device *device, const struct er_usb_config *config,
                        struct er_usb_transition *transition);
/* Add functions using their supplied settings; nonzero IDs and non-NULL strings replace the saved
 * identity. Remove preserves the saved settings of the remaining functions.
 */
int er_add_usb_functions(er_device *device, const struct er_usb_config *additions,
                         struct er_usb_transition *transition);
int er_remove_usb_functions(er_device *device, uint32_t functions,
                            struct er_usb_transition *transition);
int er_finish_usb_config(er_device *device, struct er_usb_transition *transition);
/* Prepare leaves USB unbound. For ADB, start adbd against status.adb_path before activate.
 * IDs and HID reports belong to the caller.
 */
int er_prepare_usb(er_device *device, const struct er_usb_config *config);
int er_activate_usb(er_device *device);
/* Disconnect and unbind while retaining prepared functions and mounts.
 * For ADB, deactivate before stopping adbd, then release or reconfigure.
 */
int er_deactivate_usb(er_device *device);
/* Release this handle's claim and restore the previous firmware USB policy.
 * Does not force stock storage export or change a pre-existing network claim.
 */
int er_release_usb(er_device *device);

#ifdef __cplusplus
}
#endif
#endif
