# Use the HAL from Python or C

[Overview](../README.md) · [Installation](getting-started.md) · [USB guide](usb.md)

Use the same HAL release for the C headers, shared library, and Python bindings.
Hardware examples run on a **supported reader**. Catalogue queries can also run
on a Linux computer with a native build.

## Check a control before using it

In the reader's shell, run:

```sh
python3 - <<'PYCODE'
from ereader_hal import Capability, Device

with Device() as device:
    print(device.get_info().model)
    brightness = device.get_capability(Capability.BRIGHTNESS)
    if brightness.available:
        print("Brightness:", device.get_brightness())
    else:
        print("Brightness unavailable:", brightness.reason)
PYCODE
```

To change brightness after the availability check, call
`device.set_brightness(30)`. Brightness and warmth use percentages.
Availability can change after a check, so also handle errors from the operation.
[Capability fields](devices.md#capability-results) explain what each result means.

### Handle lifetime and errors

- Serialize calls on each `Device` or `er_device` handle. The library does not
  make concurrent calls on one handle safe.
- Firmware queries can block. In a graphical app, run them in a worker and pass
  results to the UI. The worker must still serialize access to its handle.
- Close handles explicitly. Python supports `with Device() as device`.
  A failed close leaves the handle open so an application can resolve the
  cleanup problem and retry. Do not rely on garbage collection for USB cleanup.
- Most C functions return zero or a negative `errno`. Python raises `OSError`.
  Capability reports and operation-result objects carry positive error numbers.
- Python information objects are snapshots and remain valid after close.
  The C pointer returned by `er_get_info()` belongs to its device handle.

## Read the model and battery in C

Save this as `reader-info.c` in the HAL checkout:

```c
#include "ereader_hal.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    er_device *device = NULL;
    int result = er_open(&device);
    if (result < 0) {
        fprintf(stderr, "Open: %s\n", strerror(-result));
        return 1;
    }

    printf("Model: %s\n", er_get_info(device)->model);
    struct er_battery battery;
    result = er_get_battery(device, &battery);
    if (result == 0)
        printf("Battery: %d%%\n", battery.percent);
    else
        fprintf(stderr, "Battery: %s\n", strerror(-result));

    int closed = er_close(device);
    if (closed < 0)
        fprintf(stderr, "Close: %s\n", strerror(-closed));
    return result < 0 || closed < 0;
}
```

For a `kindlehf` reader, build on a **Linux computer** with the matching
[toolchain and HAL build](development.md#cross-compile-for-a-reader):

```sh
arm-kindlehf-linux-gnueabihf-gcc -std=c11 -Wall -Wextra -Iinclude \
  reader-info.c build/libereader-hal.a -lrt -ldl -lm -o reader-info
```

The archive links the HAL into the executable; firmware libraries are still
required. Do not run this ARM executable on the build computer. With root SSH
already configured on the reader, copy and run it from the computer's Bash shell:

```sh
read -r -p 'Reader IP address: ' reader_ip
scp -O reader-info "root@$reader_ip:/tmp/reader-info"
ssh "root@$reader_ip" /tmp/reader-info
```

Use the compiler for your own firmware ABI if it differs from `kindlehf`.

## Read events without blocking the UI

`poll_event()` reads input and queued events. It does not query firmware status.
Call `sample_status()` on your application's chosen schedule to queue battery,
USB, cover, and watched capability changes.

Save this as `events.py` and run `python3 events.py` in the reader's shell.
It observes events for ten seconds:

```python
import time
from ereader_hal import Capability, Device

with Device() as device:
    device.watch_capabilities(Capability.BRIGHTNESS | Capability.WIFI_STATUS)
    end = time.monotonic() + 10
    next_sample = 0.0
    while time.monotonic() < end:
        now = time.monotonic()
        if now >= next_sample:
            device.sample_status()
            next_sample = time.monotonic() + 1
        event = device.poll_event(timeout_ms=100)
        if event is not None:
            print(event)
```

In C, `er_poll_event()` returns 1 for an event, 0 for a timeout, or a negative
error. An `INPUT_RESET` event means the application must discard held buttons
and touch contacts. Lifecycle callbacks are separate: they run synchronously
around HAL-requested suspend, not every firmware sleep. A prepare-sleep callback
can veto suspend. Do not call back into the same handle from that callback.

## Work with Wi-Fi networks

The Wi-Fi API provides status, scanning, saved profiles, and connection control.
Start with `get_wifi_capabilities()` to check supported security modes and
profile-editing operations. Both backends write open and WPA-PSK profiles;
other security types may be reported without being writable.

| Task | Python method |
| --- | --- |
| Read connection and address | `get_wifi_status()` |
| Scan nearby networks | `wifi_scan()` |
| List saved profiles | `wifi_list_networks()` |
| Add or edit a saved profile | `wifi_add_network()` / `wifi_update_network()` with `WifiConfig` |
| Remove a saved profile | `wifi_remove_network()` |
| Connect or disconnect | `wifi_connect()` / `wifi_disconnect()` |
| Observe completion without resending a request | `wifi_wait_connection()` |

Profile changes persist. Treat network IDs as opaque and list them again after
reconnecting or rebooting. Passwords are write-only; `None` preserves an existing
password during an update. Connecting normally waits for an IP address.

`WifiResult.completion` is `CONFIRMED`, `FAILED`, or `UNCERTAIN`. Python errors
raise `WifiOperationError` with the result attached. If completion is uncertain,
read status or wait for completion instead of repeating the change. Timeouts
bound completion polling; native firmware calls have their own service timeouts.

## API reference

| Area | C reference | Python reference |
| --- | --- | --- |
| Device, lighting, battery, events, and power | [ereader_hal.h](../include/ereader_hal.h) | [Device](../python/ereader_hal/device.py) |
| Capability results and reasons | [ereader_hal_types.h](../include/ereader_hal_types.h) | [Types and enums](../python/ereader_hal/types.py) |
| Wi-Fi | [ereader_hal_wifi.h](../include/ereader_hal_wifi.h) | [Device methods](../python/ereader_hal/device.py) |
| USB gadgets and transitions | [ereader_hal_usb.h](../include/ereader_hal_usb.h) | [USB guide](usb.md) |

`get_battery_details()` uses `None` for missing measurements in Python. The C
structure uses a `present` field mask. Do not interpret an absent measurement
as zero. Power APIs include nested suspend inhibitors and a renewable awake
lease; renew a lease before its timeout or call `release_awake()` to end it early.
