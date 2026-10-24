#define _GNU_SOURCE
#include "linux/framebuffer.h"
#include "core/device.h"
#include "core/events.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>

#include <sys/ioctl.h>
#include <unistd.h>

int er_read_display(struct er_display *display)
{
    int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    struct fb_var_screeninfo info;
    int result = ioctl(fd, FBIOGET_VSCREENINFO, &info) < 0 ? -errno : 0;
    close(fd);
    if (!result)
        *display = (struct er_display){info.xres, info.yres, info.bits_per_pixel, info.rotate};
    return result;
}

int er_get_display(const er_device *d, struct er_display *display)
{
    if (!display)
        return -EINVAL;
    int result = er_supported(d, ER_DISPLAY);
    if (result)
        return result;
    struct er_display value;
    result = er_read_display(&value);
    if (result)
        return result;
    if (value.rotation > 3)
        return -EIO;
    value.rotation = d->profile->rotation_map[value.rotation] * 90;
    *display = value;
    return 0;
}

int er_set_rotation(er_device *d, unsigned degrees)
{
    if (degrees > 270 || degrees % 90)
        return -EINVAL;
    int result = er_supported(d, ER_ROTATION);
    if (result)
        return result;
    int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) < 0) {
        result = -errno;
        goto done;
    }
    unsigned target;
    for (target = 0; target < 4; ++target)
        if (d->profile->rotation_map[target] == degrees / 90)
            break;
    if (target == 4) {
        result = -ENOSYS;
        goto done;
    }
    if (info.rotate == target)
        goto done;
    info.rotate = target;
    if (d->profile->rotation_quirk == ER_ROTA_INVERT_ALL ||
        (d->profile->rotation_quirk == ER_ROTA_INVERT_ODD && (target & 1)))
        info.rotate ^= 2;
    info.activate = FB_ACTIVATE_NOW;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) < 0) {
        result = -errno;
        goto done;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) < 0) {
        result = -errno;
        goto done;
    }
    result = info.rotate == target ? 0 : -EIO;
    /* Cancel active contacts even on a readback mismatch: coordinates changed. */
    er_input_reset(d);
    if (!result)
        d->input_rotation = degrees;
done:
    close(fd);
    return result;
}
