# USB networking and gadgets

[Overview](../README.md) · [Programming](programming.md) · [Device reference](devices.md)

A USB gadget is the device interface a host computer sees: networking, ADB,
HID, serial, or a combination. The HAL prepares and binds gadgets. It does not
start SSH, run `adbd`, configure host networking, or provide a file-transfer UI.
The CLI's `usb` command reads cable status; gadget changes use the C or Python API.

## File storage and ownership

Firmware owns file storage. `USBMode.FILESTORAGE` and `USBFunction.STORAGE` are
status values only. Mode setters and gadget configuration reject them.
The HAL cannot export user storage or a disk image.

On Kindle, a USB network claim lasts until `release_usb()` or device close.
Releasing it restores the previous firmware USB policy without requesting a
storage transition. An existing external network claim remains in place.
Otherwise, normal USB events, such as reconnecting the cable, let firmware
select storage.

Applications and services must release files on user storage before firmware
exports it. Preparing a separate runtime cache and stopping those consumers
are responsibilities of the application or its supervisor, not the HAL.
Stock storage mode can interrupt Wi-Fi and make applications installed on
user storage unavailable.

`get_usb()` describes cable and local mount status. `get_usb_mode()` and
`get_usb_gadget()` describe the gadget. A mounted or writable userstore reported
by `get_usb()` does not prove that the host is not using its backing storage.

## Claim native Kindle networking

Use this on a **Kindle**, as root, with `volumd` running and unpaused. If the
computer has mounted the Kindle's storage, eject that volume first. The
transition can disconnect a USB SSH session, so run from a local shell or an
independent connection.

Save as `usb-network.py` on the reader, then run `python3 usb-network.py` in its
shell:

```python
from ereader_hal import Device, USBMode

with Device() as device:
    transition = device.set_usb_mode(USBMode.NETWORK)
    print("Observed mode:", transition.active)
    print("Interface:", device.get_usb_gadget().network_interface)
    input("Press Enter to release this network claim. ")
```

The handle stays open while waiting. Closing it releases the claim. The host
and reader still need compatible IP addresses and an SSH server if you want
SSH access. A network gadget alone does not provide those services.

For C, use `er_set_usb_mode(device, ER_USB_MODE_NETWORK, &transition)` and retain
the handle until you call `er_release_usb()` or `er_close()`.

## Prepare a custom or composite gadget

Custom gadgets require kernel configfs support, the requested function drivers,
and an available USB controller. Probe individual functions with
`get_usb_function()` and combinations with `get_usb_combination()`.

1. Create a `USBConfig` with your USB vendor/product IDs, manufacturer, product,
   and serial strings. Include HID descriptor and report settings when using HID.
2. Call `configure_usb(config)`. This copies the template without changing USB.
3. Call `set_usb_mode()` with `NETWORK`, `ADB`, `HID`, or `SERIAL`. Combine flags
   with `|`, such as `USBMode.NETWORK | USBMode.ADB`.
4. Keep the handle open for the session and complete any pending preparation.
5. Release the gadget when finished, following the ADB order below if applicable.

`USBConfig.functions` selects ECM and/or RNDIS for network mode; an empty
network selection uses ECM. Other functions come from the requested mode.
`configure_usb(None)` selects native Kindle network handling for later requests;
it does not itself change the active gadget.

On Kindle, custom handover first asks firmware to leave stock storage and
remount userstore. The HAL then pauses that `volumd` process while it owns the
gadget. It saves and restores stock network parameters, resumes the daemon,
and restores the previous USB policy on release. This path needs `g_ether`
and the configfs gadget drivers. Firmware MTP and other foreign gadgets are
not taken over. On Kobo, the custom-gadget API requires a free controller.

### Complete an ADB transition

The HAL does not bundle or launch an ADB daemon. Use an `adbd` provider that
can open FunctionFS endpoints at `get_usb_gadget().adb_path`.

When preparation returns `PENDING`, start that provider against the reported
path, then call `finish_usb_mode()`. Before switching away from active ADB:

1. Call `deactivate_usb()`.
2. Stop `adbd` and wait for it to release its endpoints.
3. Request the next mode or call `release_usb()`.

The daemon's invocation depends on the provider; there is no universal HAL
`adbd` command.

## Read transition results

| Field or state | Meaning |
| --- | --- |
| `ACTIVE` | The requested gadget is bound; host enumeration may still be in progress. |
| `PENDING` | Preparation needs completion, usually ADB endpoint setup. |
| `BLOCKED` | The request failed; inspect the error and observed state. |
| `active` | Observed mode flags, not a copy of the requested flags. |
| `error` | The operation's positive error number. |
| `cleanup_error`, `restore_error` | Failures while cleaning up or restoring the previous state. |
| `cleanup_pending` | Cleanup still needs attention. |

Python raises `USBTransitionError` with a `transition` object on failure.
In C, inspect `struct er_usb_mode_transition` as well as the return code.
Cleanup failure keeps the handle open for a retry; applications must retain
it until cleanup succeeds.

The lower-level API also supports `apply_usb_config()`, adding or removing
functions, and separate prepare/activate steps. See the
[USB header](../include/ereader_hal_usb.h) and
[Python methods](../python/ereader_hal/device.py) for their result structures.
