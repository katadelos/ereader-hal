# Installation and first use

[Overview](../README.md) · [Command-line use](command-line.md)

## Before you install

Use a reader that can run homebrew software, with a shell and permission to
access its hardware. Kindle requires a jailbreak. Most controls need root.
Python is optional; the bindings require Python 3.14 or later on the reader.

Build the CLI and shared library for the reader's userspace ABI, C library,
and kernel baseline. Follow [the build instructions](development.md), including
[cross-compilation](development.md#cross-compile-for-a-reader) when building on
a computer. A desktop build cannot run on an ARM reader.

If you already have a compatible package, follow its installation instructions.
The steps below install a source build in its own directory on user storage.

## Copy a source build to the reader

After building, run from the **HAL checkout on your Linux computer**, in Bash.
This example uses an existing root SSH connection to the reader:

```sh
tar --exclude='__pycache__' -czf /tmp/ereader-hal.tar.gz \
  build/ereader-hal build/libereader-hal.so python/ereader_hal LICENSE
read -r -p 'Reader IP address: ' reader_ip
scp -O /tmp/ereader-hal.tar.gz "root@$reader_ip:/tmp/ereader-hal.tar.gz"
```

In the **reader's shell**, choose an installation directory. This example uses
Kindle's `/mnt/us`; on Kobo, set `HAL_ROOT=/mnt/onboard/ereader-hal` instead:

```sh
HAL_ROOT=/mnt/us/ereader-hal
mkdir -p "$HAL_ROOT"
tar -xzf /tmp/ereader-hal.tar.gz -C "$HAL_ROOT"
```

Set these paths in each shell where you use the HAL, or in your application's
launcher. Use the same `HAL_ROOT` as above:

```sh
export PATH="$HAL_ROOT/build:$PATH"
export EREADER_HAL_LIBRARY="$HAL_ROOT/build/libereader-hal.so"
export PYTHONPATH="$HAL_ROOT/python${PYTHONPATH:+:$PYTHONPATH}"
```

These commands do not install Python or other runtime libraries. They must
already be available for the reader's ABI. For distribution packages, see
[package integration](package-integration.md).

## Inspect the reader

Run in the reader's shell after setting the paths above:

```sh
ereader-hal info
ereader-hal capabilities
ereader-hal battery
```

`info` identifies the reader. `capabilities` distinguishes hardware support
from an implemented control and its current availability. Check the reason
printed under a control if it is unavailable.

To look up another model without opening its hardware:

```sh
ereader-hal models
ereader-hal info 'Kobo Sage'
```

With Python 3.14 or later available as `python3`, try the bindings:

```sh
python3 - <<'PYCODE'
from ereader_hal import Device

with Device() as device:
    print(device.get_info().model)
    print(device.get_battery())
PYCODE
```

Continue with [shell commands](command-line.md) or the
[Python and C examples](programming.md).

## Update the HAL

Close applications using the HAL before upgrading, then start them again.
For a package installation, use its package manager. For a source installation,
replace the installation directory with a freshly extracted build. Keep the
shared library and Python bindings from the same source revision.

Keep user storage mounted on the reader while using files installed there.
[USB ownership](usb.md#file-storage-and-ownership) explains this limitation.

## Common problems

| Symptom | What to check |
| --- | --- |
| `ereader-hal` is not found | Check the installation directory and `PATH`. |
| `No module named ereader_hal` | Check `PYTHONPATH` and use Python 3.14 or later. |
| Python cannot load `libereader-hal.so` | Check `EREADER_HAL_LIBRARY` and use a library built for the reader's ABI. |
| Opening a device fails with `ENODEV` | Check [model detection](devices.md#model-detection). A normal desktop is not a supported device. |
| A control fails with a permission or service error | Inspect `ereader-hal capabilities`, run with the needed permissions, and check that the firmware service is running. |
| Hardware is listed but the control is unavailable | Read its `Implemented`, `Available`, and reason fields. See the [known limits](devices.md#known-limits). |
| A command is slow | Firmware queries are synchronous. Run them outside a graphical application's UI thread. |

For a bug report, include the failing command and its output, the package
version or source commit, and the relevant capability result. For a source
build, `git rev-parse HEAD` in the build checkout identifies the commit.
