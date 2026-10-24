# Command-line use

[Overview](../README.md) · [Installation](getting-started.md) · [Programming](programming.md)

Run these commands in the **reader's shell** after installation.
`ereader-hal --help` prints the command list. Each invocation opens and closes
its own device handle, except for catalogue lookups.

## Read device information

| Command | Result |
| --- | --- |
| `ereader-hal models` | All catalogue model names; no hardware access. |
| `ereader-hal info 'Kobo Sage'` | Static facts for that exact catalogue name. |
| `ereader-hal info` | Detected model, hardware facts, and runtime display dimensions. |
| `ereader-hal capabilities` | Supported, implemented, available, and blocked states, with reasons. |
| `ereader-hal battery` | Battery percentage and charging state. |
| `ereader-hal usb` | Cable, external-power, and local storage-mount status. |
| `ereader-hal display` | Width, height, bit depth, and rotation. |
| `ereader-hal wifi` | Whether the Wi-Fi radio is enabled. |

USB mount status does not establish exclusive ownership of storage. It is not
permission to export a mounted filesystem. See the [USB guide](usb.md).

## Change lighting

Check the relevant capability first. For example, on a reader with a working
brightness control:

```sh
ereader-hal brightness
ereader-hal brightness 30
```

Brightness and warmth use percentages from 0 to 100, mapped to the device's
native steps. Other lighting commands are:

| Command | Action |
| --- | --- |
| `ereader-hal warmth` | Read warmth. |
| `ereader-hal warmth 40` | Set warmth to 40%. |
| `ereader-hal light` | Read frontlight state and brightness. |
| `ereader-hal light off` | Turn off the frontlight where supported. |

To turn the light back on, set a positive brightness, such as
`ereader-hal brightness 30`. The CLI also accepts `light on`, but it cannot
restore a level saved by an earlier invocation: remembered brightness belongs
to one open handle. Use a persistent [Python or C handle](programming.md) when
your app needs that state. Automatic-brightness bindings are not implemented;
the CLI's `auto-brightness on|off` command cannot enable them.

## Control Wi-Fi

```sh
ereader-hal wifi on
```

`ereader-hal wifi off` turns the radio off and can disconnect a Wi-Fi SSH
session. The CLI does not scan or edit saved networks. Use the firmware's
network settings or the [Wi-Fi API](programming.md#work-with-wi-fi-networks).

## Observe input and status

```sh
ereader-hal events 30
```

This listens for 30 seconds and samples firmware status once a second.
Omit the duration to run until Ctrl-C. Input is observed without being grabbed;
the firmware can still respond to the same buttons and touches.

`ereader-hal rotate 90` changes supported framebuffer rotation. It does not
redraw the screen. The application must redraw, and touch coordinates follow
the resulting screen orientation. Valid rotations are 0, 90, 180, and 270.

## Control sleep and wake

These commands need the corresponding power capabilities and suitable
permissions. Test suspend from a local shell or with a recovery path: a reader
that sleeps can lose its network connection.

| Command | Action |
| --- | --- |
| `ereader-hal hold hal 60` | Block cooperating HAL suspend requests for 60 seconds. |
| `ereader-hal hold system 60` | Also hold a kernel wake lock where supported. |
| `ereader-hal wake 60` | Set an RTC alarm for 60 seconds from now without suspending. |
| `ereader-hal suspend 60` | Set an alarm, suspend, and remove its alarm after returning. |
| `ereader-hal suspend` | Request suspend without setting an alarm. |
| `ereader-hal wake-reason` | Read the available wake reason and IRQ information. |
| `ereader-hal cancel-wake` | Cancel the RTC alarm, including one set by another application. |

Timed operations accept 1–86400 seconds. An existing RTC alarm is not replaced.
Another wake source can resume the device before a timed suspend expires.
HAL inhibitors do not control independent firmware sleep decisions; system
inhibitors depend on the kernel's wake-lock interface.
