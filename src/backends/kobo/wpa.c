#define _GNU_SOURCE
#include "wpa.h"
#include "core/device.h"
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int socket_path(const er_device *d, struct er_wpa *wpa, struct sockaddr_un *address)
{
    glob_t paths = {0};
    int result = glob("/var/run/wpa_supplicant/*", 0, NULL, &paths);
    if (result) {
        globfree(&paths);
        return result == GLOB_NOMATCH ? -ENOENT : -EIO;
    }
    char selected[16] = "";
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        const char *name = strrchr(paths.gl_pathv[i], '/') + 1;
        struct stat info;
        size_t length = strlen(name);
        if (length >= sizeof(selected) || !strcmp(name, "global"))
            continue;
        if (lstat(paths.gl_pathv[i], &info) || !S_ISSOCK(info.st_mode))
            continue;
        if (!selected[0] || !strcmp(name, d->wifi_interface))
            memcpy(selected, name, length + 1);
        if (!strcmp(selected, d->wifi_interface))
            break;
    }
    globfree(&paths);
    if (!selected[0])
        return -ENOENT;
    snprintf(wpa->interface, sizeof(wpa->interface), "%s", selected);
    address->sun_family = AF_UNIX;
    snprintf(address->sun_path, sizeof(address->sun_path), "/var/run/wpa_supplicant/%s", selected);
    return 0;
}

int er_wpa_open(const er_device *d, struct er_wpa *wpa)
{
    *wpa = (struct er_wpa){.fd = -1, .timeout_ms = 3000};
    struct sockaddr_un remote = {0}, local = {.sun_family = AF_UNIX};
    int result = socket_path(d, wpa, &remote);
    if (result)
        return result;
    snprintf(wpa->directory, sizeof(wpa->directory), "/tmp/ereader-hal-wpa-XXXXXX");
    if (!mkdtemp(wpa->directory)) {
        wpa->directory[0] = '\0';
        return -errno;
    }
    snprintf(wpa->path, sizeof(wpa->path), "%s/control", wpa->directory);
    snprintf(local.sun_path, sizeof(local.sun_path), "%s", wpa->path);
    wpa->fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (wpa->fd < 0 || bind(wpa->fd, (struct sockaddr *)&local, sizeof(local)) ||
        connect(wpa->fd, (struct sockaddr *)&remote, sizeof(remote))) {
        result = -errno;
        er_wpa_close(wpa);
        return result;
    }
    return 0;
}

void er_wpa_close(struct er_wpa *wpa)
{
    if (wpa->fd >= 0)
        close(wpa->fd);
    if (wpa->path[0])
        unlink(wpa->path);
    if (wpa->directory[0])
        rmdir(wpa->directory);
    wpa->fd = -1;
}

int er_wpa_receive(const struct er_wpa *wpa, char *reply, size_t size, int timeout_ms)
{
    struct pollfd pollfd = {.fd = wpa->fd, .events = POLLIN};
    int result;
    int64_t deadline = er_now_ms() + timeout_ms;
    do {
        int64_t remaining = deadline - er_now_ms();
        result = poll(&pollfd, 1, remaining > 0 ? (int)remaining : 0);
    } while (result < 0 && errno == EINTR);
    if (result <= 0)
        return result ? -errno : -ETIMEDOUT;
    ssize_t received = recv(wpa->fd, reply, size - 1, MSG_TRUNC);
    if (received < 0)
        return -errno;
    if ((size_t)received >= size)
        return -EOVERFLOW;
    reply[received] = '\0';
    while (received && (reply[received - 1] == '\n' || reply[received - 1] == '\r'))
        reply[--received] = '\0';
    return 0;
}

int er_wpa_request(struct er_wpa *wpa, const char *command, char *reply, size_t size)
{
    if (wpa->uncertain)
        return -EPROTO;
    if (send(wpa->fd, command, strlen(command), 0) < 0)
        return -errno;
    int result = er_wpa_receive(wpa, reply, size, wpa->timeout_ms);
    if (result) {
        wpa->uncertain = true;
        return result;
    }
    if (!strncmp(reply, "FAIL-BUSY", 9))
        return -EBUSY;
    if (!strncmp(reply, "FAIL", 4))
        return -EINVAL;
    if (!strcmp(reply, "UNKNOWN COMMAND"))
        return -ENOTSUP;
    return 0;
}

int er_wpa_ok(struct er_wpa *wpa, const char *command)
{
    char reply[128];
    int result = er_wpa_request(wpa, command, reply, sizeof(reply));
    return result ? result : strcmp(reply, "OK") ? -EPROTO : 0;
}

static int config_path(const struct er_wpa *wpa, char *path, size_t size)
{
    glob_t paths = {0};
    if (glob("/proc/[0-9]*/cmdline", 0, NULL, &paths)) {
        globfree(&paths);
        return -ENOENT;
    }
    int result = -ENOENT;
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        char command[8192];
        int fd = open(paths.gl_pathv[i], O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        ssize_t length = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (length <= 0)
            continue;
        command[length] = '\0';
        const char *program = strrchr(command, '/');
        if (strcmp(program ? program + 1 : command, "wpa_supplicant"))
            continue;
        const char *interface = NULL, *config = NULL;
        for (char *arg = command + strlen(command) + 1; arg < command + length;
             arg += strlen(arg) + 1) {
            if (arg[0] != '-' || (arg[1] != 'i' && arg[1] != 'c'))
                continue;
            const char *value = arg + 2;
            if (!*value) {
                value = arg + strlen(arg) + 1;
                if (value >= command + length)
                    break;
            }
            if (arg[1] == 'i')
                interface = value;
            else
                config = value;
        }
        if (interface && config && !strcmp(interface, wpa->interface)) {
            result = snprintf(path, size, "%s", config) >= (int)size ? -ENAMETOOLONG : 0;
            break;
        }
    }
    globfree(&paths);
    return result;
}

int er_wpa_persistence(const struct er_wpa *wpa)
{
    char path[1024];
    int result = config_path(wpa, path, sizeof(path));
    if (result)
        return result;
    FILE *file = fopen(path, "r");
    if (!file)
        return -errno;
    char line[512];
    int update = 0;
    while (fgets(line, sizeof(line), file)) {
        int value;
        if (sscanf(line, " update_config = %d", &value) == 1)
            update = value;
    }
    result = ferror(file) ? -EIO : 0;
    fclose(file);
    if (result)
        return result;
    if (update != 1)
        return -EROFS;
    if (access(path, W_OK))
        return -errno;
    char *slash = strrchr(path, '/');
    if (!slash)
        return -EINVAL;
    *slash = '\0';
    return access(path[0] ? path : "/", W_OK) ? -errno : 0;
}
