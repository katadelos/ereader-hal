#ifndef EREADER_HAL_TYPES_H
#define EREADER_HAL_TYPES_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct er_device er_device;

enum er_capability_reason {
    ER_REASON_NONE,
    ER_REASON_UNSUPPORTED,
    ER_REASON_NOT_IMPLEMENTED,
    ER_REASON_PERMISSION,
    ER_REASON_DRIVER_MISSING,
    ER_REASON_SERVICE_UNAVAILABLE,
    ER_REASON_RADIO_OFF,
    ER_REASON_RESOURCE_BUSY,
    ER_REASON_TIMEOUT,
    ER_REASON_IO_ERROR,
    ER_REASON_INVALID_CONFIGURATION
};

/* supported: hardware feature; implemented: HAL binding; available: usable now.
 * blocked identifies a temporary constraint. error is a positive errno.
 * Numeric controls report their range and number of steps.
 */
struct er_capability_info {
    bool supported;
    bool implemented;
    bool available;
    bool blocked;
    int error;
    int minimum;
    int maximum;
    int steps;
    enum er_capability_reason reason_code;
    char reason[128];
};

const char *er_capability_reason_name(enum er_capability_reason reason);

#ifdef __cplusplus
}
#endif
#endif
