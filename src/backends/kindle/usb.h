#ifndef ER_KINDLE_USB_H
#define ER_KINDLE_USB_H
#include "ereader_hal.h"
#include <netinet/in.h>
#include <sys/types.h>

struct er_kindle_usb {
    bool detached;
    bool policy_saved, policy_changed, paused_by_hal;
    int original_policy;
    pid_t volume_pid;
    unsigned long long volume_start;
    struct in_addr address, netmask;
    short flags;
    char parameters[16][256];
    unsigned parameter_count;
    char device_mac[32], host_mac[32];
};

int er_kindle_usb_policy(int *policy);
int er_kindle_usb_service_ready(void);
int er_kindle_usb_mode(uint32_t *mode);
int er_kindle_usb_set_policy(int policy);
int er_kindle_usb_claim(struct er_kindle_usb *state, bool *reenumerated);
int er_kindle_usb_return_network(struct er_kindle_usb *state);
int er_kindle_usb_acquire(struct er_kindle_usb *state, const struct er_usb_config *config);
int er_kindle_usb_detach(struct er_kindle_usb *state, const char *controller);
int er_kindle_usb_address(const struct er_kindle_usb *state);
int er_kindle_usb_restore(struct er_kindle_usb *state);
#endif
