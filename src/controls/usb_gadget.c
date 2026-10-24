#define _GNU_SOURCE
#include "backends/kindle/lipc_api.h"
#include "core/device.h"
#include "linux/io.h"
#include "linux/usb_configfs.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <unistd.h>

int er_get_usb(const er_device *d, struct er_usb *usb)
{
    if (!usb)
        return -EINVAL;
    int result = er_supported(d, ER_USB);
    if (result)
        return result;
    glob_t paths = {0};
    int found = glob("/sys/class/udc/*/state", 0, NULL, &paths);
    if (found || paths.gl_pathc != 1) {
        globfree(&paths);
        return -ENODEV;
    }
    char state[128];
    result = er_read_text(paths.gl_pathv[0], state, sizeof(state));
    globfree(&paths);
    if (result)
        return result;
    state[strcspn(state, "\n")] = 0;
    struct er_usb value = {0};
    glob_t supplies = {0};
    if (glob("/sys/class/power_supply/*/online", 0, NULL, &supplies)) {
        globfree(&supplies);
        return -ENODEV;
    }
    for (size_t i = 0; i < supplies.gl_pathc; ++i) {
        int online;
        result = er_read_int(supplies.gl_pathv[i], &online);
        if (result)
            break;
        value.external_power |= online != 0;
    }
    globfree(&supplies);
    if (result)
        return result;
    if (!strcmp(state, "configured"))
        value.state = ER_USB_DATA;
    else if (strcmp(state, "not attached"))
        value.state = ER_USB_CONNECTED;
    else
        value.state = value.external_power ? ER_USB_CHARGING : ER_USB_DISCONNECTED;
    const char *storage = d->vendor == ER_KOBO ? "/mnt/onboard" : "/mnt/us";
    FILE *mounts = setmntent("/proc/mounts", "r");
    if (!mounts)
        return -errno;
    struct mntent mount;
    const struct mntent *entry;
    char buffer[4096];
    while ((entry = getmntent_r(mounts, &mount, buffer, sizeof(buffer)))) {
        if (!strcmp(entry->mnt_dir, storage)) {
            value.storage_mounted = true;
            struct statvfs fs;
            if (statvfs(storage, &fs)) {
                result = -errno;
                break;
            }
            value.storage_writable = !(fs.f_flag & ST_RDONLY) && access(storage, W_OK) == 0;
            break;
        }
    }
    endmntent(mounts);
    if (result)
        return result;
    *usb = value;
    return 0;
}

struct er_usb_config *er_usb_copy_config(const struct er_usb_config *config)
{
    const char *strings[] = {config->manufacturer,
                             config->product,
                             config->serial,
                             config->device_mac,
                             config->host_mac};
    size_t size = sizeof(*config) + config->hid_descriptor_size;
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); ++i)
        if (strings[i])
            size += strlen(strings[i]) + 1;
    struct er_usb_config *copy = malloc(size);
    if (!copy)
        return NULL;
    *copy = *config;
    const char **fields[] = {
        &copy->manufacturer, &copy->product, &copy->serial, &copy->device_mac, &copy->host_mac};
    char *next = (char *)(copy + 1);
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); ++i) {
        if (!strings[i])
            continue;
        size_t length = strlen(strings[i]) + 1;
        memcpy(next, strings[i], length);
        *fields[i] = next;
        next += length;
    }
    if (config->hid_descriptor_size) {
        memcpy(next, config->hid_descriptor, config->hid_descriptor_size);
        copy->hid_descriptor = (const uint8_t *)next;
    }
    return copy;
}

static int transition_result(const er_device *d, struct er_usb_transition *status, int error)
{
    const struct er_usb_session *session = d->usb_session;
    status->error = -error;
    if (session) {
        status->active = session->bound ? session->functions : 0;
        status->prepared = session->prepared;
        status->reenumerated |= session->reenumerated;
        status->cleanup_pending = session->cleanup_pending;
        if (session->stock_network && !session->created) {
            uint32_t mode = 0;
            if (!er_kindle_usb_mode(&mode) && mode == ER_USB_MODE_NETWORK)
                status->active = ER_USB_ECM | ER_USB_RNDIS;
        }
    }
    return error;
}

static int restore_config(er_device *d, struct er_usb_transition *status)
{
    struct er_usb_session *session = d->usb_session;
    int error = session->transition_error;
    int result;
    if (!session->restoring) {
        result = er_usb_clear_session(d);
        if (result) {
            status->cleanup_error = -result;
            return transition_result(d, status, error);
        }
        free(session->config);
        session->config = session->previous;
        session->previous = NULL;
        session->restoring = true;
    }
    if (!session->config && session->previous_native) {
        result = er_kindle_usb_return_network(&session->stock);
        status->restore_error = -result;
        status->restored = !result;
        if (!result) {
            session->transition = session->restoring = false;
            session->transition_error = 0;
            session->requested = 0;
        }
        return transition_result(d, status, error);
    }
    if (!session->config) {
        status->reenumerated |= session->reenumerated;
        result = er_release_usb(d);
        if (result)
            status->cleanup_error = -result;
        else
            status->restored = true;
        return transition_result(d, status, error);
    }
    if (!session->prepared) {
        result = er_usb_clear_session(d);
        if (!result)
            result = er_usb_prepare_session(d, session->config);
        if (result) {
            status->restore_error = -result;
            return transition_result(d, status, error);
        }
    }
    result = session->previous_bound ? er_activate_usb(d) : 0;
    if (result && result != -EAGAIN) {
        status->restore_error = -result;
        return transition_result(d, status, error);
    }
    status->restored = result == 0;
    status->restore_error = -result;
    session->transition = false;
    session->restoring = false;
    session->transition_error = 0;
    session->requested = session->config->functions;
    return transition_result(d, status, error);
}

static int invalid_transition(struct er_usb_transition *status)
{
    if (status)
        *status = (struct er_usb_transition){.error = EINVAL};
    return -EINVAL;
}

int er_finish_usb_config(er_device *d, struct er_usb_transition *status)
{
    if (!d || !status || !d->usb_session)
        return invalid_transition(status);
    struct er_usb_session *session = d->usb_session;
    *status = (struct er_usb_transition){.requested = session->requested ? session->requested
                                                                         : session->functions};
    if (session->transition_error)
        return restore_config(d, status);
    int result = er_activate_usb(d);
    if (result && result != -EAGAIN && session->transition) {
        session->transition_error = result;
        return restore_config(d, status);
    }
    if (!result) {
        free(session->previous);
        session->previous = NULL;
        session->transition = false;
    }
    return transition_result(d, status, result);
}

int er_apply_usb_config(er_device *d, const struct er_usb_config *config,
                        struct er_usb_transition *status)
{
    if (!d || !status)
        return invalid_transition(status);
    *status = (struct er_usb_transition){.requested = config ? config->functions : 0};
    if (!config) {
        bool bound = d->usb_session && d->usb_session->bound;
        if (d->usb_session)
            d->usb_session->reenumerated = false;
        int result = er_release_usb(d);
        status->reenumerated = !result && bound;
        status->cleanup_error = -result;
        return transition_result(d, status, result);
    }
    int result = er_usb_validate_config(d, config);
    if (result)
        return transition_result(d, status, result);
    if (d->usb_session && (d->usb_session->transition || d->usb_session->cleanup_pending))
        return transition_result(d, status, -EBUSY);
    struct er_usb_config *copy = er_usb_copy_config(config);
    if (!copy)
        return transition_result(d, status, -ENOMEM);
    if (!d->usb_session)
        result = er_usb_new_session(d);
    if (result) {
        free(copy);
        return transition_result(d, status, result);
    }
    struct er_usb_session *session = d->usb_session;
    session->previous_native = session->stock_network && !session->config;
    session->previous = session->config;
    session->previous_bound = session->bound;
    session->config = copy;
    session->requested = config->functions;
    session->transition = true;
    session->reenumerated = false;
    result = er_usb_clear_session(d);
    if (result) {
        session->transition_error = result;
        status->cleanup_error = -result;
        return transition_result(d, status, result);
    }
    result = er_usb_prepare_session(d, copy);
    if (result) {
        session->transition_error = result;
        return restore_config(d, status);
    }
    if ((copy->functions & ER_USB_ADB) && access("/dev/usb-ffs/adb/ep1", F_OK))
        return transition_result(d, status, 0);
    return er_finish_usb_config(d, status);
}

int er_add_usb_functions(er_device *d, const struct er_usb_config *additions,
                         struct er_usb_transition *status)
{
    if (!d || !additions || !status || !additions->functions)
        return invalid_transition(status);
    if (!d->usb_session || !d->usb_session->config)
        return er_apply_usb_config(d, additions, status);
    struct er_usb_config config = *d->usb_session->config;
    config.functions |= additions->functions;
    if (additions->vendor_id)
        config.vendor_id = additions->vendor_id;
    if (additions->product_id)
        config.product_id = additions->product_id;
    if (additions->manufacturer)
        config.manufacturer = additions->manufacturer;
    if (additions->product)
        config.product = additions->product;
    if (additions->serial)
        config.serial = additions->serial;
    if (additions->device_mac)
        config.device_mac = additions->device_mac;
    if (additions->host_mac)
        config.host_mac = additions->host_mac;
    if (additions->functions & ER_USB_HID) {
        config.hid_descriptor = additions->hid_descriptor;
        config.hid_descriptor_size = additions->hid_descriptor_size;
        config.hid_report_length = additions->hid_report_length;
        config.hid_protocol = additions->hid_protocol;
        config.hid_subclass = additions->hid_subclass;
    }
    config.take_stock_usb |= additions->take_stock_usb;
    return er_apply_usb_config(d, &config, status);
}

int er_remove_usb_functions(er_device *d, uint32_t functions, struct er_usb_transition *status)
{
    if (!d || !status || !d->usb_session || !d->usb_session->config || !functions ||
        (functions & ~ER_USB_FUNCTIONS))
        return invalid_transition(status);
    struct er_usb_config config = *d->usb_session->config;
    config.functions &= ~functions;
    return er_apply_usb_config(d, config.functions ? &config : NULL, status);
}
