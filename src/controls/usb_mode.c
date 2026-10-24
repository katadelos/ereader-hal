#include "core/device.h"
#include "linux/usb_configfs.h"
#include <errno.h>
#include <stdlib.h>

#define MODES ((1u << 5) - 1)

static uint32_t mode_from_functions(uint32_t functions)
{
    uint32_t mode = 0;
    if (functions & ER_USB_STORAGE)
        mode |= ER_USB_MODE_FILESTORAGE;
    if (functions & (ER_USB_ECM | ER_USB_RNDIS))
        mode |= ER_USB_MODE_NETWORK;
    if (functions & ER_USB_ADB)
        mode |= ER_USB_MODE_ADB;
    if (functions & ER_USB_HID)
        mode |= ER_USB_MODE_HID;
    if (functions & ER_USB_SERIAL)
        mode |= ER_USB_MODE_SERIAL;
    return mode;
}

int er_configure_usb(er_device *d, const struct er_usb_config *config)
{
    if (!d)
        return -EINVAL;
    struct er_usb_config *copy = NULL;
    if (config) {
        if (config->functions & ~ER_USB_FUNCTIONS)
            return -EINVAL;
        if (config->functions & ER_USB_STORAGE)
            return -EOPNOTSUPP;
        struct er_usb_config check = *config;
        check.functions = ER_USB_ECM;
        int result = er_usb_validate_config(d, &check);
        if (result)
            return result;
        copy = er_usb_copy_config(config);
        if (!copy)
            return -ENOMEM;
    }
    free(d->usb_config);
    d->usb_config = copy;
    return 0;
}

int er_get_usb_mode(const er_device *d, uint32_t *mode)
{
    if (!d || !mode)
        return -EINVAL;
    if (d->usb_session && d->usb_session->created) {
        struct er_usb_gadget gadget;
        int result = er_get_usb_gadget(d, &gadget);
        if (!result)
            *mode = gadget.bound ? mode_from_functions(gadget.functions) : 0;
        return result;
    }
    if (d->vendor == ER_KINDLE)
        return er_kindle_usb_mode(mode);
    struct er_usb_gadget gadget;
    int result = er_get_usb_gadget(d, &gadget);
    if (!result)
        *mode = gadget.bound ? mode_from_functions(gadget.functions) : 0;
    return result;
}

static int blocked(struct er_usb_mode_transition *status, int error)
{
    status->error = -error;
    status->state = ER_USB_MODE_BLOCKED;
    return error;
}

static int mode_transition(er_device *d, uint32_t mode, const struct er_usb_transition *transition,
                           int result, struct er_usb_mode_transition *status)
{
    *status = (struct er_usb_mode_transition){
        .requested = mode,
        .active = mode_from_functions(transition->active),
        .prepared = transition->prepared,
        .reenumerated = transition->reenumerated,
        .restored = transition->restored,
        .cleanup_pending = transition->cleanup_pending,
        .error = transition->error,
        .cleanup_error = transition->cleanup_error,
        .restore_error = transition->restore_error,
        .state = result ? ER_USB_MODE_BLOCKED : ER_USB_MODE_PENDING,
    };
    /* A failed first transition can restore firmware ownership instead of a HAL configuration. */
    if (!status->active)
        er_get_usb_mode(d, &status->active);
    if (!result && status->active == mode)
        status->state = ER_USB_MODE_ACTIVE;
    return result;
}

static int claim_network(er_device *d, struct er_usb_mode_transition *status)
{
    if (d->usb_session && (d->usb_session->transition || d->usb_session->cleanup_pending))
        return blocked(status, -EBUSY);
    if (d->usb_session)
        d->usb_session->reenumerated = false;
    int result = d->usb_session ? er_usb_clear_session(d) : er_usb_new_session(d);
    if (result) {
        status->cleanup_error = -result;
        status->cleanup_pending = d->usb_session != NULL;
        er_get_usb_mode(d, &status->active);
        return blocked(status, result);
    }
    struct er_usb_session *session = d->usb_session;
    status->reenumerated = session->reenumerated;
    if (session->stock_network)
        result = er_kindle_usb_return_network(&session->stock);
    else
        session->stock_network = true;
    if (!result)
        result = er_kindle_usb_claim(&session->stock, &status->reenumerated);
    if (result) {
        int cleanup = er_release_usb(d);
        status->cleanup_error = -cleanup;
        status->cleanup_pending = cleanup != 0;
        status->restored = !cleanup;
    } else {
        free(session->config);
        session->config = NULL;
        session->requested = 0;
    }
    er_get_usb_mode(d, &status->active);
    if (result)
        return blocked(status, result);
    status->state = ER_USB_MODE_ACTIVE;
    return 0;
}

int er_set_usb_mode(er_device *d, uint32_t mode, struct er_usb_mode_transition *status)
{
    if (!status)
        return -EINVAL;
    *status = (struct er_usb_mode_transition){.requested = mode, .state = ER_USB_MODE_BLOCKED};
    if (!d || !mode || (mode & ~MODES))
        return blocked(status, -EINVAL);
    er_get_usb_mode(d, &status->active);
    if (mode & ER_USB_MODE_FILESTORAGE)
        return blocked(status, -EOPNOTSUPP);

    if (!d->usb_config && d->vendor == ER_KINDLE && mode == ER_USB_MODE_NETWORK)
        return claim_network(d, status);
    if (!d->usb_config)
        return blocked(status, -ENODATA);

    struct er_usb_config config = *d->usb_config;
    config.functions = 0;
    config.take_stock_usb = d->vendor == ER_KINDLE;
    if (mode & ER_USB_MODE_NETWORK) {
        uint32_t network = d->usb_config->functions & (ER_USB_ECM | ER_USB_RNDIS);
        config.functions |= network ? network : ER_USB_ECM;
    }
    if (mode & ER_USB_MODE_ADB)
        config.functions |= ER_USB_ADB;
    if (mode & ER_USB_MODE_HID)
        config.functions |= ER_USB_HID;
    if (mode & ER_USB_MODE_SERIAL)
        config.functions |= ER_USB_SERIAL;
    struct er_usb_transition transition;
    int result = er_apply_usb_config(d, &config, &transition);
    return mode_transition(d, mode, &transition, result, status);
}

int er_finish_usb_mode(er_device *d, struct er_usb_mode_transition *status)
{
    if (!status)
        return -EINVAL;
    *status = (struct er_usb_mode_transition){.state = ER_USB_MODE_BLOCKED};
    if (!d || !d->usb_session)
        return blocked(status, -EINVAL);
    uint32_t requested = mode_from_functions(d->usb_session->requested);
    struct er_usb_transition transition;
    int result = er_finish_usb_config(d, &transition);
    return mode_transition(d, requested, &transition, result, status);
}
