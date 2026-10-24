# ereader-hal

ereader-hal is a C hardware abstraction library, a command-line tool, and Python
bindings for Kindle and Kobo readers. It is for developers building homebrew
apps and people writing scripts to inspect or control their reader.

It provides one API for hardware that differs between models and firmware.
Your app can check whether a control is usable before showing it to the user.

## What you can do

- Identify a reader and inspect its hardware features.
- Read battery, charging, Wi-Fi, and USB status.
- Adjust brightness and warmth, and manage Wi-Fi connections where supported.
- Read touch, button, cover, and orientation events where bindings exist.
- Schedule wake alarms, request suspend, and keep a device awake.
- Claim USB networking or configure ADB, HID, serial, and composite gadgets.

The HAL reports display geometry and can change supported framebuffer rotation.
Drawing and screen refresh belong to your application's display backend.
USB file storage belongs to the reader's firmware.

## Start here

The device catalogue covers **Kindle 4 and later** and **Kobo Touch and later**.
A catalogue entry does not mean every control works on that model. Firmware,
drivers, services, and permissions determine what is available.
See [device support and limitations](docs/devices.md).

On a reader, you need a shell and permission to access its hardware. Kindle
requires a jailbreak for homebrew access. Most control operations need root.
Install the CLI and shared library built for your reader. The optional Python
bindings require **Python 3.14 or later**.

Follow [installation and first use](docs/getting-started.md). Once installed,
start with these read-only commands in the reader's shell:

```sh
ereader-hal info
ereader-hal capabilities
ereader-hal battery
```

There is no graphical app or background service to launch. For development on
a Linux computer, [build the library and inspect the catalogue](docs/development.md).
Hardware operations require a supported reader; there is no built-in simulator.

## Guides

| Goal | Guide |
| --- | --- |
| Install or update the HAL | [Installation and first use](docs/getting-started.md) |
| Inspect a reader or change a setting from a shell | [Command-line use](docs/command-line.md) |
| Add hardware controls to a Python or C app | [Programming guide](docs/programming.md) |
| Claim USB networking or prepare a custom gadget | [USB guide](docs/usb.md) |
| Understand model detection and capability results | [Device reference](docs/devices.md) |
| Build the project or contribute a change | [Development](docs/development.md) |
| Package the library for an application or distribution | [Package integration](docs/package-integration.md) |

## Help and contributions

Check [common problems](docs/getting-started.md#common-problems), then use the
[issue tracker](https://github.com/katadelos/ereader-hal/issues).
Include the device model, firmware, HAL version, the command or small example
that failed, and the relevant capability result. Remove serial numbers,
network names, and credentials before sharing logs.

The [development guide](docs/development.md#contribute) explains where to make
changes and which checks to run.

## License

ereader-hal is licensed under the [MIT license](LICENSE).
The [device reference](docs/devices.md#sources-and-attribution) credits the
KOReader and FBInk sources used for device information.
