#define _GNU_SOURCE
#include "linux/evdev.h"
#include "core/device.h"
#include "core/events.h"
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Linux UAPI values absent from the K4-era toolchain headers. */
#define ER_ABS_MT_SLOT 0x2f
#define ER_EVIOCGMTSLOTS(length) _IOC(_IOC_READ, 'E', 0x0a, length)
#define ER_SYN_DROPPED 3

static void restore_slots(struct er_input *input);

static bool bit_set(const unsigned char *bits, unsigned code)
{
    return (bits[code / 8] & (1u << (code % 8))) != 0;
}

static const struct {
    unsigned code;
    enum er_button button;
} buttons[] = {
    {KEY_POWER, ER_BUTTON_POWER},
    {KEY_F23, ER_BUTTON_PREVIOUS},
    {KEY_F24, ER_BUTTON_NEXT},
    {KEY_PAGEUP, ER_BUTTON_PREVIOUS},
    {KEY_PAGEDOWN, ER_BUTTON_NEXT},
    {KEY_HOME, ER_BUTTON_HOME},
    {KEY_BACK, ER_BUTTON_BACK},
    {KEY_MENU, ER_BUTTON_MENU},
    {KEY_UP, ER_BUTTON_UP},
    {KEY_DOWN, ER_BUTTON_DOWN},
    {KEY_LEFT, ER_BUTTON_LEFT},
    {KEY_RIGHT, ER_BUTTON_RIGHT},
    {KEY_ENTER, ER_BUTTON_SELECT},
    {191, ER_BUTTON_NEXT},
    {193, ER_BUTTON_PREVIOUS},
};

static void clear_input(struct er_input *input)
{
    if (input->touch && input->fd >= 0) {
        int x_code = input->single ? ABS_X : ABS_MT_POSITION_X;
        int y_code = input->single ? ABS_Y : ABS_MT_POSITION_Y;
        ioctl(input->fd, EVIOCGABS(x_code), &input->x_axis);
        ioctl(input->fd, EVIOCGABS(y_code), &input->y_axis);
    }
    memset(input->current, 0, sizeof(input->current));
    memset(input->previous, 0, sizeof(input->previous));
    input->slot = 0;
    input->touch_down = false;
    for (unsigned i = 0; i < ER_CONTACT_COUNT; ++i) {
        input->current[i].id = (int)i;
        input->current[i].x = input->x_axis.value;
        input->current[i].y = input->y_axis.value;
    }
}

void er_input_clear(er_device *d)
{
    for (unsigned i = 0; i < ER_INPUT_COUNT; ++i)
        clear_input(&d->input[i]);
}

int er_input_open(er_device *d)
{
    for (unsigned i = 0; i < ER_INPUT_COUNT; ++i)
        if (d->input[i].fd < 0)
            d->input[i].error = ENODEV;
    glob_t paths = {0};
    if (glob("/dev/input/event*", 0, NULL, &paths)) {
        globfree(&paths);
        return -ENODEV;
    }
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        bool already_open = false;
        for (unsigned j = 0; j < ER_INPUT_COUNT; ++j)
            if (d->input[j].fd >= 0 && !strcmp(d->input[j].path, paths.gl_pathv[i]))
                already_open = true;
        if (already_open)
            continue;
        int fd = open(paths.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            for (unsigned j = 0; j < ER_INPUT_COUNT; ++j)
                if (d->input[j].fd < 0)
                    d->input[j].error = errno;
            continue;
        }
        unsigned char axes[(ABS_CNT + 7) / 8] = {0}, keys[(KEY_CNT + 7) / 8] = {0};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(axes)), axes);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
        bool is_touch = d->info.panel_width > 0 && d->info.panel_height > 0 &&
                        bit_set(keys, BTN_TOUCH) &&
                        (bit_set(axes, ABS_MT_POSITION_X) ||
                         (bit_set(axes, ABS_X) && !bit_set(keys, BTN_TOOL_PEN)));
        bool is_keys = bit_set(keys, KEY_POWER) || bit_set(keys, KEY_F23) ||
                       bit_set(keys, KEY_F24) || bit_set(keys, KEY_PAGEUP) ||
                       bit_set(keys, KEY_PAGEDOWN) || bit_set(keys, KEY_HOME) ||
                       bit_set(keys, KEY_LEFT);
        int index = -1;
        if (is_touch && (d->implemented & ER_TOUCH) && d->input[1].fd < 0)
            index = 1;
        else if (!is_touch && is_keys) {
            for (unsigned j = 0; j < ER_INPUT_COUNT; ++j)
                if (j != 1 && d->input[j].fd < 0) {
                    index = (int)j;
                    break;
                }
        }
        if (index < 0) {
            close(fd);
            continue;
        }
        struct er_input *input = &d->input[index];
        input->touch = is_touch;
        input->single = !bit_set(axes, ABS_MT_POSITION_X);
        snprintf(input->path, sizeof(input->path), "%s", paths.gl_pathv[i]);
        if (input->touch) {
            int x_code = input->single ? ABS_X : ABS_MT_POSITION_X;
            int y_code = input->single ? ABS_Y : ABS_MT_POSITION_Y;
            if (ioctl(fd, EVIOCGABS(x_code), &input->x_axis) < 0 ||
                ioctl(fd, EVIOCGABS(y_code), &input->y_axis) < 0) {
                input->error = errno;
                close(fd);
                continue;
            }
            if (input->x_axis.maximum <= input->x_axis.minimum ||
                input->y_axis.maximum <= input->y_axis.minimum) {
                input->error = EIO;
                close(fd);
                continue;
            }
            struct input_absinfo slot;
            input->slots = bit_set(axes, ER_ABS_MT_SLOT);
            if (input->slots && ioctl(fd, EVIOCGABS(ER_ABS_MT_SLOT), &slot) < 0) {
                input->error = errno;
                close(fd);
                continue;
            }
            if (input->slots && (slot.minimum != 0 || slot.maximum >= ER_CONTACT_COUNT)) {
                input->error = EOVERFLOW;
                close(fd);
                continue;
            }
        }
        clear_input(input);
        input->fd = fd;
        if (input->slots)
            restore_slots(input);
        input->error = 0;
    }
    globfree(&paths);
    return 0;
}

void er_input_close(er_device *d)
{
    for (unsigned i = 0; i < ER_INPUT_COUNT; ++i) {
        if (d->input[i].fd >= 0)
            close(d->input[i].fd);
        d->input[i].fd = -1;
    }
}

static int scale_axis(int value, const struct input_absinfo *axis, unsigned length)
{
    if (value < axis->minimum)
        value = axis->minimum;
    if (value > axis->maximum)
        value = axis->maximum;
    return (int)((int64_t)(value - axis->minimum) * (length - 1) / (axis->maximum - axis->minimum));
}

static void touch_event(er_device *d, struct er_input *input, const struct er_contact *contact,
                        enum er_action action)
{
    int width = (int)d->info.panel_width, height = (int)d->info.panel_height;
    int x, y;
    if (d->profile->swap_axes) {
        x = scale_axis(contact->y, &input->y_axis, (unsigned)width);
        y = scale_axis(contact->x, &input->x_axis, (unsigned)height);
    } else {
        x = scale_axis(contact->x, &input->x_axis, (unsigned)width);
        y = scale_axis(contact->y, &input->y_axis, (unsigned)height);
    }
    if (d->profile->mirror_x)
        x = width - 1 - x;
    if (d->profile->mirror_y)
        y = height - 1 - y;
    int screen_x = x, screen_y = y;
    switch (d->input_rotation) {
    case 90:
        screen_x = y;
        screen_y = width - 1 - x;
        break;
    case 180:
        screen_x = width - 1 - x;
        screen_y = height - 1 - y;
        break;
    case 270:
        screen_x = height - 1 - y;
        screen_y = x;
        break;
    }
    struct er_event event = {.type = ER_EVENT_TOUCH};
    event.data.touch.id = contact->id;
    event.data.touch.x = screen_x;
    event.data.touch.y = screen_y;
    event.data.touch.action = action;
    er_queue_event(d, &event);
}

static void finish_touch_frame(er_device *d, struct er_input *input)
{
    struct er_display display;
    if (!er_get_display(d, &display) && display.rotation != d->input_rotation) {
        d->input_rotation = display.rotation;
        er_input_reset(d);
        return;
    }
    for (unsigned i = 0; i < ER_CONTACT_COUNT; ++i) {
        struct er_contact *old = &input->previous[i];
        const struct er_contact *now = &input->current[i];
        if (old->active && (!now->active || old->id != now->id))
            touch_event(d, input, old, ER_RELEASE);
        if (now->active) {
            if (!old->active || old->id != now->id)
                touch_event(d, input, now, ER_PRESS);
            else if (old->x != now->x || old->y != now->y)
                touch_event(d, input, now, ER_MOVE);
        }
        *old = *now;
    }
}

static void restore_slots(struct er_input *input)
{
    int ids[ER_CONTACT_COUNT + 1] = {ABS_MT_TRACKING_ID};
    int xs[ER_CONTACT_COUNT + 1] = {ABS_MT_POSITION_X};
    int ys[ER_CONTACT_COUNT + 1] = {ABS_MT_POSITION_Y};
    if (ioctl(input->fd, ER_EVIOCGMTSLOTS(sizeof(ids)), ids) < 0 ||
        ioctl(input->fd, ER_EVIOCGMTSLOTS(sizeof(xs)), xs) < 0 ||
        ioctl(input->fd, ER_EVIOCGMTSLOTS(sizeof(ys)), ys) < 0)
        return;
    struct input_absinfo slots;
    if (ioctl(input->fd, EVIOCGABS(ER_ABS_MT_SLOT), &slots) < 0)
        return;
    input->slot = slots.value;
    for (int i = 0; i <= slots.maximum && i < ER_CONTACT_COUNT; ++i)
        input->current[i] = (struct er_contact){
            .id = ids[i + 1], .x = xs[i + 1], .y = ys[i + 1], .active = ids[i + 1] >= 0};
}

static void handle_input(er_device *d, struct er_input *input, const struct input_event *raw)
{
    if (raw->type == EV_SYN && raw->code == ER_SYN_DROPPED) {
        er_input_reset(d);
        input->dropped = true;
        return;
    }
    if (input->dropped) {
        if (raw->type == EV_SYN && raw->code == SYN_REPORT) {
            input->dropped = false;
            if (input->slots) {
                restore_slots(input);
                finish_touch_frame(d, input);
            }
        }
        return;
    }
    if (input->touch) {
        if (input->single) {
            struct er_contact *contact = &input->current[0];
            if (raw->type == EV_ABS && raw->code == ABS_X)
                contact->x = raw->value;
            else if (raw->type == EV_ABS && raw->code == ABS_Y)
                contact->y = raw->value;
            else if (raw->type == EV_ABS && raw->code == ABS_PRESSURE)
                contact->active = raw->value > 0;
            else if (raw->type == EV_KEY && raw->code == BTN_TOUCH)
                contact->active = raw->value != 0;
            else if (raw->type == EV_SYN && raw->code == SYN_REPORT)
                finish_touch_frame(d, input);
            return;
        }
        if (raw->type == EV_ABS) {
            if (raw->code == ER_ABS_MT_SLOT || (!input->slots && raw->code == ABS_MT_TRACKING_ID)) {
                /* Forma uses tracking IDs as persistent slot indices. */
                input->slot = raw->value;
                if (input->slots)
                    return;
            }
            if (input->slot < 0 || input->slot >= ER_CONTACT_COUNT)
                return;
            struct er_contact *contact = &input->current[input->slot];
            if (!input->slots && raw->code == ABS_MT_TOUCH_MAJOR && raw->value == 0)
                contact->active = false;
            else if (raw->code == ABS_MT_POSITION_X)
                contact->x = raw->value;
            else if (raw->code == ABS_MT_POSITION_Y)
                contact->y = raw->value;
            else if (raw->code == ABS_MT_TRACKING_ID) {
                contact->id = raw->value;
                contact->active = raw->value >= 0;
            }
        } else if (!input->slots && raw->type == EV_KEY && raw->code == BTN_TOUCH) {
            input->touch_down = raw->value != 0;
            if (!input->touch_down)
                for (unsigned i = 0; i < ER_CONTACT_COUNT; ++i)
                    input->current[i].active = false;
        } else if (raw->type == EV_SYN && raw->code == SYN_REPORT) {
            /* NTX drivers may omit an unchanged tracking ID and coordinates
             * across successive taps. Preserve queried axis state on open. */
            bool active = false;
            for (unsigned i = 0; i < ER_CONTACT_COUNT; ++i)
                active |= input->current[i].active;
            if (!input->slots && input->touch_down && !active && input->slot >= 0 &&
                input->slot < ER_CONTACT_COUNT)
                input->current[input->slot].active = true;
            finish_touch_frame(d, input);
        }
        return;
    }
    struct er_event event = {0};
    if (raw->type == EV_KEY && raw->value >= 0 && raw->value <= 2) {
        if (raw->code == KEY_F1 && d->vendor == ER_KOBO) {
            if (raw->value == 2)
                return;
            event.type = ER_EVENT_COVER;
            event.data.cover_closed = raw->value != 0;
            d->last_cover = event.data.cover_closed;
            d->cover_valid = true;
        } else {
            event.type = ER_EVENT_BUTTON;
            unsigned code = raw->code;
            if (d->profile->display_driver == ER_EINKFB) {
                if (code == KEY_PAGEUP)
                    code = KEY_F24;
                else if (code == KEY_PAGEDOWN)
                    code = KEY_F23;
                else if (code == 194)
                    code = KEY_ENTER;
            }
            size_t i = 0;
            while (i < sizeof(buttons) / sizeof(buttons[0]) && buttons[i].code != code)
                ++i;
            if (i == sizeof(buttons) / sizeof(buttons[0]))
                return;
            event.data.button.button = buttons[i].button;
            event.data.button.action = (enum er_action)raw->value;
        }
    } else if (raw->type == EV_MSC && raw->code == MSC_RAW && d->vendor == ER_KOBO) {
        event.type = ER_EVENT_ORIENTATION;
        switch (raw->value) {
        case 0x18:
            event.data.orientation = 0;
            break;
        case 0x19:
            event.data.orientation = 90;
            break;
        case 0x17:
            event.data.orientation = 180;
            break;
        case 0x1a:
            event.data.orientation = 270;
            break;
        default:
            return;
        }
    } else
        return;
    er_queue_event(d, &event);
}

int er_input_poll(er_device *d, int timeout_ms)
{
    struct pollfd fds[ER_INPUT_COUNT];
    for (unsigned i = 0; i < ER_INPUT_COUNT; ++i)
        fds[i] = (struct pollfd){.fd = d->input[i].fd, .events = POLLIN};
    int ready = poll(fds, ER_INPUT_COUNT, timeout_ms);
    if (ready < 0)
        return -errno;
    for (unsigned i = 0; i < ER_INPUT_COUNT; ++i) {
        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close(d->input[i].fd);
            d->input[i].fd = -1;
            d->input[i].error = ENODEV;
            er_input_reset(d);
            er_status_error(d, d->input[i].touch ? ER_TOUCH : ER_BUTTONS, -ENODEV);
        } else if (fds[i].revents & POLLIN) {
            struct input_event raw[32];
            ssize_t count = read(fds[i].fd, raw, sizeof(raw));
            if (count < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                return -errno;
            }
            if (!count || count % sizeof(raw[0]))
                return -EIO;
            for (size_t j = 0; j < (size_t)count / sizeof(raw[0]); ++j)
                handle_input(d, &d->input[i], &raw[j]);
        }
    }
    return 0;
}

int er_input_capability_probe(er_device *d, enum er_capability capability)
{
    er_input_open(d);
    int result = -ENODEV;
    for (unsigned i = 0; i < ER_INPUT_COUNT; ++i) {
        const struct er_input *input = &d->input[i];
        if (input->fd < 0) {
            if (input->error != ENODEV && input->error)
                result = -input->error;
            continue;
        }
        if (input->touch) {
            if (capability == ER_TOUCH)
                return 0;
            continue;
        }
        if (capability == ER_TOUCH)
            continue;
        unsigned char bits[(KEY_CNT + 7) / 8] = {0};
        unsigned type = capability == ER_ORIENTATION ? EV_MSC : EV_KEY;
        if (ioctl(input->fd, EVIOCGBIT(type, sizeof(bits)), bits) < 0) {
            result = -errno;
            continue;
        }
        if (capability == ER_ORIENTATION && bit_set(bits, MSC_RAW))
            return 0;
        if (capability == ER_COVER && bit_set(bits, KEY_F1))
            return 0;
        if (capability == ER_BUTTONS) {
            if (bit_set(bits, 194))
                return 0;
            for (size_t j = 0; j < sizeof(buttons) / sizeof(buttons[0]); ++j)
                if (bit_set(bits, buttons[j].code))
                    return 0;
        }
    }
    return result;
}

int er_get_cover(er_device *d, bool *closed)
{
    if (!closed)
        return -EINVAL;
    int result = er_supported(d, ER_COVER);
    if (result)
        return result;
    if (d->vendor == ER_KINDLE) {
        int value;
        result = er_read_int("/sys/devices/platform/eink_hall/hall_detect", &value);
        if (result)
            return result;
        if (value != 0 && value != 1)
            return -EIO;
        *closed = value != 0;
        return 0;
    }
    er_input_open(d);
    if (d->input[0].fd < 0)
        return -d->input[0].error;
    unsigned char keys[(KEY_CNT + 7) / 8] = {0};
    if (ioctl(d->input[0].fd, EVIOCGKEY(sizeof(keys)), keys) < 0)
        return -errno;
    *closed = bit_set(keys, KEY_F1);
    return 0;
}
