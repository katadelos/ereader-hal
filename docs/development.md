# Build and contribute

[Overview](../README.md) · [Programming](programming.md) · [Package integration](package-integration.md)

## Build on Linux

You need Git, Make, a C11 compiler, and Linux development headers and libraries.
The build links the system realtime, dynamic-loading, and math libraries.
The Python package requires Python 3.14 or later.

Run on a **Linux computer**:

```sh
git clone https://github.com/katadelos/ereader-hal.git ereader-hal
cd ereader-hal
make -j2 all shared
./build/ereader-hal models
./build/ereader-hal info 'Kobo Sage'
```

The outputs are `build/ereader-hal`, `build/libereader-hal.a`, and
`build/libereader-hal.so`. The catalogue commands work without a reader.
Opening a live device on a normal desktop fails with `ENODEV`.
There is no built-in device simulator.

The source depends on Linux interfaces. On macOS or Windows, use a Linux
virtual machine or container.
A desktop catalogue build does not validate firmware behavior or USB handover.

## Try Python on the build computer

From the HAL checkout, after building a shared library for that computer:

```sh
python3.14 -m venv "$HOME/.venvs/ereader-hal"
. "$HOME/.venvs/ereader-hal/bin/activate"
python -m pip install ./python
export EREADER_HAL_LIBRARY="$PWD/build/libereader-hal.so"
python - <<'PYCODE'
from ereader_hal import model_info, models

print("Catalogue entries:", len(models()))
print(model_info("Kobo Sage"))
PYCODE
```

Installing the Python package does not compile or install the C library.
`EREADER_HAL_LIBRARY` selects a library explicitly; without it, the bindings
ask the system loader for `libereader-hal.so`. A library built for ARM cannot
be loaded by a desktop Python process of another architecture.

## Cross-compile for a reader

Use a compiler that matches the reader's userspace ABI, C library, and kernel
baseline. CPU model alone does not determine the package ABI.

For a source checkout that has not been built for another architecture, this
example builds for **Kindle hard-float firmware** on a Linux computer. First put
the matching compiler's `bin` directory on `PATH`:

```sh
make -j2 all shared \
  CC=arm-kindlehf-linux-gnueabihf-gcc \
  AR=arm-kindlehf-linux-gnueabihf-ar
```

The Makefile does not separate architectures within `build/`. Do not mix host
and device object files; use a separate checkout for each target. See
[package integration](package-integration.md) to stage the resulting files.

[`scripts/build-local.sh`](../scripts/build-local.sh) is a Docker wrapper for an
existing toolchain installation. Set `TOOLCHAIN_ROOT` to the directory containing
your toolchains, `TOOLCHAIN` to the compiler prefix, and `BUILD_IMAGE` to an
existing Linux ARM64 build image before running it.

## Source map

| Location | Purpose |
| --- | --- |
| `include/` | Public C API and result structures. |
| `src/profiles/` | Device identities, hardware facts, and model bindings. |
| `src/backends/` | Kindle firmware and Kobo hardware/service interfaces. |
| `src/controls/` | Shared lighting, network, USB, battery, and power operations. |
| `src/core/` | Handle lifetime, capabilities, and event handling. |
| `src/linux/` | Linux input, framebuffer metadata, process, and USB helpers. |
| `cli/main.c` | Command-line interface. |
| `python/ereader_hal/` | ctypes declarations, value types, and Python methods. |

## Contribute

Use the [issue tracker](https://github.com/katadelos/ereader-hal/issues) to
report a problem or discuss a new device binding. Include the firmware version
and observed interface, rather than inferring controls solely from a model name.
Submit changes as a pull request.

Keep device facts separate from implemented controls. An unsupported or missing
binding should produce an accurate capability result. Update C structures and
ctypes declarations together when changing the API, and keep examples in sync.
Preserve [third-party attribution](devices.md#sources-and-attribution).

For C changes, the Makefile provides these checks; install `clang-format`,
`cppcheck`, and `shellcheck` in the Linux development environment first:

```sh
make format-check lint
```

For Python changes, install Ruff in your development environment and run:

```sh
make python-format-check python-lint
```

`make format` and `make python-format` apply the configured formatting. Build
changed C code with `make all shared` and check relevant behavior on a reader.
For documentation-only edits, check local links, headings, code examples, and
`git diff --check`; hardware builds are unnecessary unless an example needs one.
No active Git hooks are supplied by this repository.
