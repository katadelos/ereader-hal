#define _GNU_SOURCE
#include "linux/usb_configfs.h"
#include "backends/kindle/usb.h"
#include "core/capabilities.h"
#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#define CONFIGFS "/sys/kernel/config"
#define GADGET_ROOT CONFIGFS "/usb_gadget"
#define GADGET GADGET_ROOT "/ereader_hal"
#define ADB_PATH "/dev/usb-ffs/adb"

struct usb_function {
    enum er_usb_function bit;
    const char *name, *module, *dependency, *config;
};

static const struct usb_function functions[] = {
    {ER_USB_ECM, "ecm.hal", "usb_f_ecm", "u_ether", "CONFIG_USB_F_ECM=y"},
    {ER_USB_RNDIS, "rndis.hal", "usb_f_rndis", "u_ether", "CONFIG_USB_F_RNDIS=y"},
    {ER_USB_ADB, "ffs.adb", "usb_f_fs", NULL, "CONFIG_USB_F_FS=y"},
    {ER_USB_HID, "hid.hal", "usb_f_hid", NULL, "CONFIG_USB_F_HID=y"},
    {ER_USB_SERIAL, "acm.hal", "usb_f_acm", "u_serial", "CONFIG_USB_F_ACM=y"},
};

static bool exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static bool mounted(const char *path)
{
    FILE *file = setmntent("/proc/mounts", "r");
    if (!file)
        return false;
    const struct mntent *entry;
    bool found = false;
    while ((entry = getmntent(file))) {
        if (!strcmp(entry->mnt_dir, path)) {
            found = true;
            break;
        }
    }
    endmntent(file);
    return found;
}

static bool filesystem_available(const char *name)
{
    FILE *file = fopen("/proc/filesystems", "r");
    if (!file)
        return false;
    char line[128];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        char *type = strrchr(line, '\t');
        type = type ? type + 1 : line;
        type[strcspn(type, "\r\n")] = 0;
        if (!strcmp(type, name)) {
            found = true;
            break;
        }
    }
    fclose(file);
    return found;
}

static bool module_loaded(const char *module)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/module/%s", module);
    return exists(path);
}

static int module_path(const er_device *d, const char *module, char *path, size_t size)
{
    struct utsname kernel;
    if (uname(&kernel))
        return -errno;
    const char *store = d->vendor == ER_KINDLE ? "/mnt/us" : "/mnt/onboard";
    const char *locations[] = {"extra",
                               "kernel/drivers/usb/gadget/function",
                               "kernel/drivers/usb/gadget",
                               "kernel/fs/configfs"};
    for (unsigned i = 0; i < sizeof(locations) / sizeof(locations[0]); ++i) {
        snprintf(path,
                 size,
                 "%s/.eink-sdk/lib/modules/%s/%s/%s.ko",
                 store,
                 kernel.release,
                 locations[i],
                 module);
        if (!access(path, R_OK))
            return 0;
        snprintf(path, size, "/lib/modules/%s/%s/%s.ko", kernel.release, locations[i], module);
        if (!access(path, R_OK))
            return 0;
    }
    if (d->vendor == ER_KOBO) {
        char pattern[256];
        snprintf(pattern, sizeof(pattern), "/drivers/*/usb/gadget/%s.ko", module);
        glob_t matches = {0};
        glob(pattern, 0, NULL, &matches);
        bool found = matches.gl_pathc == 1;
        if (found)
            snprintf(path, size, "%s", matches.gl_pathv[0]);
        globfree(&matches);
        if (found)
            return 0;
    }
    return -ENOENT;
}

static bool listed_builtin(const char *path, const char *module)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    char line[PATH_MAX], name[128];
    snprintf(name, sizeof(name), "%s.ko", module);
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        const char *base = strrchr(line, '/');
        if (!strcmp(base ? base + 1 : line, name)) {
            found = true;
            break;
        }
    }
    fclose(file);
    return found;
}

static const char *module_config(const char *module)
{
    if (!strcmp(module, "configfs"))
        return "CONFIG_CONFIGFS_FS=y";
    if (!strcmp(module, "libcomposite"))
        return "CONFIG_USB_LIBCOMPOSITE=y";
    if (!strcmp(module, "u_ether"))
        return "CONFIG_USB_U_ETHER=y";
    if (!strcmp(module, "u_serial"))
        return "CONFIG_USB_U_SERIAL=y";
    for (unsigned i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i)
        if (!strcmp(module, functions[i].module))
            return functions[i].config;
    return NULL;
}

static bool module_builtin(const er_device *d, const char *module)
{
    struct utsname kernel;
    if (!uname(&kernel)) {
        char path[PATH_MAX];
        const char *store = d->vendor == ER_KINDLE ? "/mnt/us" : "/mnt/onboard";
        snprintf(path, sizeof(path), "/lib/modules/%s/modules.builtin", kernel.release);
        if (listed_builtin(path, module))
            return true;
        snprintf(path,
                 sizeof(path),
                 "%s/.eink-sdk/lib/modules/%s/modules.builtin",
                 store,
                 kernel.release);
        if (listed_builtin(path, module))
            return true;
    }
    const char *option = module_config(module);
    if (!option || access("/proc/config.gz", R_OK))
        return false;
    size_t size = 256 * 1024;
    char *config = malloc(size);
    if (!config)
        return false;
    char *args[] = {"/bin/zcat", "/proc/config.gz", NULL};
    int result = er_run_timeout(args, config, size, 1000);
    bool found = false;
    if (!result) {
        char *save;
        for (char *line = strtok_r(config, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!strcmp(line, option)) {
                found = true;
                break;
            }
        }
    }
    free(config);
    return found;
}

static bool function_builtin(const er_device *d, const struct usb_function *function)
{
    char path[PATH_MAX];
    snprintf(path,
             sizeof(path),
             GADGET_ROOT "/*/functions/%.*s.*",
             (int)strcspn(function->name, "."),
             function->name);
    glob_t matches = {0};
    glob(path, 0, NULL, &matches);
    bool found = matches.gl_pathc != 0;
    globfree(&matches);
    return found || module_builtin(d, function->module);
}

static int load_module(const er_device *d, const char *module)
{
    if (module_loaded(module) || module_builtin(d, module))
        return 0;
    char path[PATH_MAX];
    int result = module_path(d, module, path, sizeof(path));
    if (result)
        return result;
    char *args[] = {"/sbin/insmod", path, NULL};
    return er_run(args, NULL, 0);
}

static int controller(char *name, size_t size, char *driver, size_t driver_size)
{
    glob_t paths = {0};
    glob("/sys/class/udc/*", 0, NULL, &paths);
    if (paths.gl_pathc != 1) {
        globfree(&paths);
        return -ENODEV;
    }
    const char *base = strrchr(paths.gl_pathv[0], '/');
    snprintf(name, size, "%s", base + 1);
    char path[256], text[1024];
    snprintf(path, sizeof(path), "%s/uevent", paths.gl_pathv[0]);
    globfree(&paths);
    int result = er_read_text(path, text, sizeof(text));
    if (result)
        return result;
    driver[0] = 0;
    char *save;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
        if (!strncmp(line, "USB_UDC_DRIVER=", 15))
            snprintf(driver, driver_size, "%s", line + 15);
    return 0;
}

int er_usb_disconnect_controller(const char *controller)
{
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/udc/%s/soft_connect", controller);
    return er_write_text(path, "disconnect");
}

static uint32_t function_bit(const char *name)
{
    if (!strncmp(name, "mass_storage.", strlen("mass_storage.")))
        return ER_USB_STORAGE;
    for (unsigned i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i) {
        size_t prefix = strcspn(functions[i].name, ".");
        if (!strncmp(name, functions[i].name, prefix) && name[prefix] == '.')
            return functions[i].bit;
    }
    return 0;
}

int er_get_usb_gadget(const er_device *d, struct er_usb_gadget *status)
{
    if (!d || !status)
        return -EINVAL;
    *status = (struct er_usb_gadget){0};
    if (d->usb_session && (d->usb_session->functions & ER_USB_ADB))
        snprintf(status->adb_path, sizeof(status->adb_path), "%s", ADB_PATH);
    char driver[128], path[PATH_MAX];
    int result = controller(status->controller, sizeof(status->controller), driver, sizeof(driver));
    if (result)
        return result;
    snprintf(path, sizeof(path), "/sys/class/udc/%s/state", status->controller);
    result = er_read_line(path, status->state, sizeof(status->state));
    if (result)
        return result;
    glob_t paths = {0};
    glob(GADGET_ROOT "/*/UDC", 0, NULL, &paths);
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        char bound[64];
        if (er_read_line(paths.gl_pathv[i], bound, sizeof(bound)))
            continue;
        bool own = d->usb_session && !strcmp(paths.gl_pathv[i], GADGET "/UDC");
        if (strcmp(bound, status->controller) && !own)
            continue;
        size_t length = strlen(paths.gl_pathv[i]) - strlen("/UDC");
        if (length >= sizeof(status->gadget)) {
            globfree(&paths);
            return -ENAMETOOLONG;
        }
        memcpy(status->gadget, paths.gl_pathv[i], length);
        status->gadget[length] = 0;
        status->prepared = true;
        status->bound = bound[0] != 0;
        status->owned = own;
        if (status->bound)
            break;
    }
    globfree(&paths);
    if (!status->prepared || (driver[0] && !status->bound)) {
        snprintf(status->gadget, sizeof(status->gadget), "%s", driver);
        status->bound = driver[0] != 0;
        status->owned = false;
        if (!strcmp(driver, "g_ether")) {
            status->functions = ER_USB_ECM | ER_USB_RNDIS;
            status->owned = d->usb_session && d->usb_session->stock_network;
            snprintf(status->network_interface, sizeof(status->network_interface), "usb0");
        } else if (!strcmp(driver, "g_mass_storage") || !strcmp(driver, "g_file_storage"))
            status->functions = ER_USB_STORAGE;
        return 0;
    }
    snprintf(path, sizeof(path), "%s/configs/*/*", status->gadget);
    glob(path, 0, NULL, &paths);
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        struct stat entry;
        if (lstat(paths.gl_pathv[i], &entry) || !S_ISLNK(entry.st_mode))
            continue;
        char *target = realpath(paths.gl_pathv[i], NULL);
        if (!target)
            continue;
        const char *base = strrchr(target, '/');
        uint32_t bit = base ? function_bit(base + 1) : 0;
        status->functions |= bit;
        if (bit == ER_USB_ECM || (bit == ER_USB_RNDIS && !(status->functions & ER_USB_ECM))) {
            snprintf(path, sizeof(path), "%s/ifname", target);
            er_read_line(path, status->network_interface, sizeof(status->network_interface));
        }
        free(target);
    }
    globfree(&paths);
    return 0;
}

int er_get_usb_function(const er_device *d, enum er_usb_function function,
                        struct er_capability_info *info)
{
    if (!d || !info || !function || ((uint32_t)function & ((uint32_t)function - 1)) ||
        ((uint32_t)function & ~ER_USB_FUNCTIONS))
        return -EINVAL;
    *info = (struct er_capability_info){.supported = true, .implemented = true};
    if (function == ER_USB_STORAGE) {
        info->implemented = false;
        snprintf(info->reason, sizeof(info->reason), "Filestorage is managed by firmware");
        er_capability_finish(info, -EOPNOTSUPP);
        return 0;
    }
    char name[64], driver[128], path[PATH_MAX];
    int result = controller(name, sizeof(name), driver, sizeof(driver));
    if (!result && !exists(GADGET_ROOT) && !module_loaded("libcomposite") &&
        !module_builtin(d, "libcomposite")) {
        result = module_path(d, "libcomposite", path, sizeof(path));
        if (result)
            snprintf(info->reason, sizeof(info->reason), "USB configfs driver is unavailable");
    }
    if (!result && !mounted(CONFIGFS) && !filesystem_available("configfs")) {
        result = module_path(d, "configfs", path, sizeof(path));
        if (result)
            snprintf(info->reason, sizeof(info->reason), "Configfs filesystem is unavailable");
    }
    for (unsigned i = 0; !result && i < sizeof(functions) / sizeof(functions[0]); ++i) {
        if (functions[i].bit != function)
            continue;
        if (!module_loaded(functions[i].module) &&
            module_path(d, functions[i].module, path, sizeof(path)) &&
            function_builtin(d, &functions[i]))
            continue;
        const char *modules[] = {functions[i].dependency, functions[i].module};
        for (unsigned j = 0; !result && j < sizeof(modules) / sizeof(modules[0]); ++j) {
            if (!modules[j] || module_loaded(modules[j]) || module_builtin(d, modules[j]))
                continue;
            result = module_path(d, modules[j], path, sizeof(path));
            if (result)
                snprintf(info->reason,
                         sizeof(info->reason),
                         "Kernel module %s is unavailable",
                         modules[j]);
        }
    }
    if (!result && function == ER_USB_HID && d->profile && d->profile->usb_fixed_hid) {
        result = -EOPNOTSUPP;
        snprintf(info->reason,
                 sizeof(info->reason),
                 "The stock HID driver ignores custom report descriptors");
    }
    char own_controller[64];
    bool owns_controller = !result && d->usb_session &&
                           !er_read_line(GADGET "/UDC", own_controller, sizeof(own_controller)) &&
                           !strcmp(own_controller, name);
    if (!result && d->profile && d->profile->usb_single_gadget) {
        result = -EBUSY;
        snprintf(info->reason,
                 sizeof(info->reason),
                 "The stock driver requires its existing gadget to remain allocated");
    }
    if (!result && driver[0] && !owns_controller) {
        result = -EBUSY;
        if (d->vendor == ER_KINDLE && !strcmp(driver, "g_ether"))
            snprintf(info->reason,
                     sizeof(info->reason),
                     "Stock USB networking: set_usb_mode coordinates firmware handover");
        else
            snprintf(info->reason, sizeof(info->reason), "USB controller belongs to %.80s", driver);
    }
    if (!result && geteuid() != 0)
        result = -EACCES;
    er_capability_finish(info, result);
    if (!info->reason[0])
        snprintf(info->reason,
                 sizeof(info->reason),
                 "%s",
                 result ? strerror(-result) : "Ready for USB preparation");
    return 0;
}

int er_get_usb_combination(const er_device *d, uint32_t selected, struct er_capability_info *info)
{
    if (!d || !info || !selected || (selected & ~ER_USB_FUNCTIONS))
        return -EINVAL;
    if (selected & ER_USB_STORAGE)
        return er_get_usb_function(d, ER_USB_STORAGE, info);
    *info = (struct er_capability_info){.supported = true, .implemented = true};
    int result = 0;
    for (unsigned i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i) {
        if (!(selected & functions[i].bit))
            continue;
        struct er_capability_info function;
        int error = er_get_usb_function(d, functions[i].bit, &function);
        if (error)
            return error;
        info->supported &= function.supported;
        info->implemented &= function.implemented;
        if (function.error && !result) {
            result = -function.error;
            info->reason_code = function.reason_code;
            snprintf(info->reason, sizeof(info->reason), "%s", function.reason);
        }
    }
    if (!result)
        snprintf(info->reason,
                 sizeof(info->reason),
                 "Functions available; controller checks endpoint capacity at activation");
    er_capability_finish(info, result);
    return 0;
}

static int directory(const char *path)
{
    return mkdir(path, 0755) ? -errno : 0;
}

static int attribute(const char *name, const char *value)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), GADGET "/%s", name);
    return er_write_text(path, value);
}

static int number(const char *name, unsigned value)
{
    char text[32];
    snprintf(text, sizeof(text), "%u", value);
    return attribute(name, text);
}

int er_usb_validate_config(const er_device *d, const struct er_usb_config *config)
{
    if (!config || !config->functions || (config->functions & ~ER_USB_FUNCTIONS) ||
        !config->vendor_id || !config->product_id || !config->manufacturer || !config->product ||
        !config->serial)
        return -EINVAL;
    if (config->functions & ER_USB_STORAGE)
        return -EOPNOTSUPP;
    if (config->hid_descriptor_size > 4096 ||
        (config->hid_descriptor_size && !config->hid_descriptor))
        return -EINVAL;
    if ((config->functions & ER_USB_HID) &&
        (!config->hid_descriptor || !config->hid_descriptor_size || !config->hid_report_length ||
         config->hid_report_length > 65535 || config->hid_protocol > 255 ||
         config->hid_subclass > 255))
        return -EINVAL;
    if (d->profile && d->profile->usb_single_gadget)
        return -EBUSY;
    if ((config->functions & ER_USB_HID) && d->profile && d->profile->usb_fixed_hid)
        return -EOPNOTSUPP;
    return 0;
}

static int prepare_function(const er_device *d, const struct usb_function *function,
                            const struct er_usb_config *config)
{
    struct er_usb_session *session = d->usb_session;
    char path[PATH_MAX], name[128];
    snprintf(path, sizeof(path), GADGET "/functions/%s", function->name);
    int result = directory(path);
    if (result == -ENOENT || result == -ENODEV) {
        result = function->dependency ? load_module(d, function->dependency) : 0;
        if (!result)
            result = load_module(d, function->module);
        if (!result)
            result = directory(path);
    }
    if (result)
        return result;
    if (function->bit == ER_USB_ECM || function->bit == ER_USB_RNDIS) {
        if (config->device_mac) {
            snprintf(name, sizeof(name), "functions/%s/dev_addr", function->name);
            result = attribute(name, config->device_mac);
        }
        if (!result && config->host_mac) {
            snprintf(name, sizeof(name), "functions/%s/host_addr", function->name);
            result = attribute(name, config->host_mac);
        }
    } else if (function->bit == ER_USB_HID) {
        result = number("functions/hid.hal/protocol", config->hid_protocol);
        if (!result)
            result = number("functions/hid.hal/subclass", config->hid_subclass);
        if (!result)
            result = number("functions/hid.hal/report_length", config->hid_report_length);
        if (!result)
            result = er_write_bytes(GADGET "/functions/hid.hal/report_desc",
                                    config->hid_descriptor,
                                    config->hid_descriptor_size);
    } else if (function->bit == ER_USB_ADB) {
        if (mounted(ADB_PATH))
            return -EBUSY;
        if (mkdir("/dev/usb-ffs", 0755) && errno != EEXIST)
            return -errno;
        if (!mkdir(ADB_PATH, 0755))
            session->created_adb = true;
        else if (errno != EEXIST)
            return -errno;
        if (mount("adb", ADB_PATH, "functionfs", 0, NULL))
            return -errno;
        session->mounted_adb = true;
    }
    if (result)
        return result;
    snprintf(name, sizeof(name), GADGET "/configs/c.1/%s", function->name);
    return symlink(path, name) ? -errno : 0;
}

int er_usb_new_session(er_device *d)
{
    if (d->usb_session)
        return -EBUSY;
    int result;
    struct er_usb_session *session = calloc(1, sizeof(*session));
    if (!session)
        return -ENOMEM;
    session->lock =
        open("/var/run/ereader-hal-usb.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (session->lock < 0) {
        result = -errno;
        free(session);
        return result;
    }
    if (flock(session->lock, LOCK_EX | LOCK_NB)) {
        result = -errno;
        close(session->lock);
        free(session);
        return result;
    }
    d->usb_session = session;
    return 0;
}

int er_usb_prepare_session(er_device *d, const struct er_usb_config *config)
{
    struct er_usb_session *session = d->usb_session;
    session->functions = config->functions;
    char driver[128];
    int result =
        controller(session->controller, sizeof(session->controller), driver, sizeof(driver));
    struct er_usb_config effective = *config;
    if (!result && config->take_stock_usb && d->vendor == ER_KINDLE &&
        !session->stock.paused_by_hal) {
        if (driver[0] && strcmp(driver, "g_ether") && strcmp(driver, "g_file_storage") &&
            strcmp(driver, "g_mass_storage"))
            result = -EBUSY;
        /* Check every function before changing firmware state; an occupied UDC is expected. */
        for (unsigned i = 0; !result && i < sizeof(functions) / sizeof(functions[0]); ++i) {
            if (!(config->functions & functions[i].bit))
                continue;
            struct er_capability_info info;
            result = er_get_usb_function(d, functions[i].bit, &info);
            if (!result && info.error != EBUSY)
                result = -info.error;
        }
        if (!result) {
            session->stock_network = true;
            result = er_kindle_usb_acquire(&session->stock, config);
        }
        if (!result)
            result = controller(
                session->controller, sizeof(session->controller), driver, sizeof(driver));
    }
    if (!result && driver[0] && !(session->stock_network && !strcmp(driver, "g_ether")))
        result = -EBUSY;
    if (!result && session->stock_network) {
        if (!effective.device_mac)
            effective.device_mac = session->stock.device_mac;
        if (!effective.host_mac)
            effective.host_mac = session->stock.host_mac;
    }
    if (!result && !mounted(CONFIGFS)) {
        if (!filesystem_available("configfs"))
            result = load_module(d, "configfs");
        if (!result && mount("configfs", CONFIGFS, "configfs", 0, NULL))
            result = -errno;
        if (!result)
            session->mounted_configfs = true;
    }
    if (!result && !exists(GADGET_ROOT))
        result = load_module(d, "libcomposite");
    if (!result)
        result = directory(GADGET);
    if (!result)
        session->created = true;
    const char *directories[] = {
        GADGET "/strings/0x409", GADGET "/configs/c.1", GADGET "/configs/c.1/strings/0x409"};
    for (unsigned i = 0; !result && i < sizeof(directories) / sizeof(directories[0]); ++i)
        result = directory(directories[i]);
    if (!result)
        result = number("idVendor", config->vendor_id);
    if (!result)
        result = number("idProduct", config->product_id);
    if (!result)
        result = attribute("strings/0x409/manufacturer", config->manufacturer);
    if (!result)
        result = attribute("strings/0x409/product", config->product);
    if (!result)
        result = attribute("strings/0x409/serialnumber", config->serial);
    if (!result)
        result = attribute("configs/c.1/strings/0x409/configuration", "eReader HAL");
    for (unsigned i = 0; !result && i < sizeof(functions) / sizeof(functions[0]); ++i)
        if (config->functions & functions[i].bit)
            result = prepare_function(d, &functions[i], &effective);
    session->prepared = result == 0;
    return result;
}

int er_prepare_usb(er_device *d, const struct er_usb_config *config)
{
    if (!d)
        return -EINVAL;
    int result = er_usb_validate_config(d, config);
    if (!result)
        result = er_usb_new_session(d);
    if (result)
        return result;
    d->usb_session->config = er_usb_copy_config(config);
    result = d->usb_session->config ? er_usb_prepare_session(d, config) : -ENOMEM;
    if (result)
        er_release_usb(d);
    return result;
}

int er_activate_usb(er_device *d)
{
    if (!d || !d->usb_session)
        return -EINVAL;
    struct er_usb_session *session = d->usb_session;
    if (!session->prepared || session->cleanup_pending)
        return -EBUSY;
    char name[64], driver[128], bound[64];
    int result = er_read_line(GADGET "/UDC", bound, sizeof(bound));
    if (result)
        return result;
    if (session->bound) {
        if (strcmp(bound, session->controller))
            return -EBUSY;
        return session->stock_network && (session->functions & (ER_USB_ECM | ER_USB_RNDIS))
                   ? er_kindle_usb_address(&session->stock)
                   : 0;
    }
    result = controller(name, sizeof(name), driver, sizeof(driver));
    if (result)
        return result;
    if (bound[0] || strcmp(name, session->controller))
        return -EBUSY;
    if ((session->functions & ER_USB_ADB) && !exists(ADB_PATH "/ep1"))
        return -EAGAIN;
    if (session->stock_network && !session->stock.detached) {
        if (strcmp(driver, "g_ether"))
            return -EBUSY;
        result = er_kindle_usb_detach(&session->stock, session->controller);
        session->reenumerated |= session->stock.detached;
        if (result)
            return result;
    } else if (driver[0])
        return -EBUSY;
    result = attribute("UDC", session->controller);
    if (!result) {
        session->bound = true;
        if (session->stock_network && (session->functions & (ER_USB_ECM | ER_USB_RNDIS)))
            result = er_kindle_usb_address(&session->stock);
    }
    return result;
}

int er_deactivate_usb(er_device *d)
{
    if (!d)
        return -EINVAL;
    struct er_usb_session *session = d->usb_session;
    if (!session || !session->created)
        return 0;
    char bound[64];
    int result = er_read_line(GADGET "/UDC", bound, sizeof(bound));
    if (result)
        return result;
    if (bound[0] && strcmp(bound, session->controller))
        return -EBUSY;
    if (bound[0]) {
        result = er_usb_disconnect_controller(session->controller);
        if (!result)
            result = attribute("UDC", "\n");
        if (result == -ENODEV && !er_read_line(GADGET "/UDC", bound, sizeof(bound)) && !bound[0])
            result = 0;
        if (result)
            return result;
        session->reenumerated = true;
    }
    session->bound = false;
    return 0;
}

static int remove_directory(const char *path)
{
    return rmdir(path) && errno != ENOENT ? -errno : 0;
}

int er_usb_clear_session(er_device *d)
{
    if (!d)
        return -EINVAL;
    struct er_usb_session *session = d->usb_session;
    if (!session)
        return 0;
    session->cleanup_pending = true;
    int result;
    if (session->created) {
        result = er_deactivate_usb(d);
        if (result)
            return result;
        session->prepared = false;
        for (unsigned i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), GADGET "/configs/c.1/%s", functions[i].name);
            if (unlink(path) && errno != ENOENT)
                return -errno;
        }
    }
    if (session->mounted_adb) {
        if (umount(ADB_PATH))
            return -errno;
        session->mounted_adb = false;
    }
    if (session->created_adb) {
        result = remove_directory(ADB_PATH);
        if (result)
            return result;
        session->created_adb = false;
    }
    if (session->created) {
        for (unsigned i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), GADGET "/functions/%s", functions[i].name);
            result = remove_directory(path);
            if (result)
                return result;
        }
        const char *directories[] = {GADGET "/configs/c.1/strings/0x409",
                                     GADGET "/configs/c.1",
                                     GADGET "/strings/0x409",
                                     GADGET};
        for (unsigned i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
            result = remove_directory(directories[i]);
            if (result)
                return result;
        }
        session->created = false;
    }
    if (session->mounted_configfs) {
        if (umount(CONFIGFS))
            return -errno;
        session->mounted_configfs = false;
    }
    session->functions = 0;
    session->cleanup_pending = false;
    return 0;
}

int er_release_usb(er_device *d)
{
    int result = er_usb_clear_session(d);
    if (result || !d->usb_session)
        return result;
    struct er_usb_session *session = d->usb_session;
    if (session->stock_network) {
        session->cleanup_pending = true;
        result = er_kindle_usb_restore(&session->stock);
        if (result)
            return result;
        session->stock_network = false;
    }
    close(session->lock);
    free(session->config);
    free(session->previous);
    free(session);
    d->usb_session = NULL;
    return 0;
}
