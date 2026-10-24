# Package integration

[Overview](../README.md) · [Installation](getting-started.md) · [Development](development.md)

This page is for application and distribution maintainers. For a standalone
installation on a reader, use the [installation guide](getting-started.md).

## Stage the library and CLI

[Build for the intended architecture](development.md#cross-compile-for-a-reader)
first. On the **Linux build computer**, from that checkout, stage the files
without installing them into the host system:

```sh
make install install-shared DESTDIR="$PWD/stage" PREFIX=/usr
install -D -m 0644 LICENSE stage/usr/share/licenses/ereader-hal/LICENSE
```

The staging tree contains:

| Files | Staged location |
| --- | --- |
| CLI | `stage/usr/bin/ereader-hal` |
| Shared C library | `stage/usr/lib/libereader-hal.so` |
| Static C library | `stage/usr/lib/libereader-hal.a` |
| Public headers | `stage/usr/include/ereader_hal*.h` |
| License | `stage/usr/share/licenses/ereader-hal/LICENSE` |

Put the CLI and shared library in the runtime package. Headers and the static
library are for development. Choose an installation prefix suitable for your
application or distribution; do not overwrite a reader's stock system libraries.

## Include Python bindings

The Makefile does not install the Python bindings. Their package metadata is in
[python/pyproject.toml](../python/pyproject.toml). They require Python 3.14 or later
with ctypes support, plus the matching shared library.

For a package targeting Python 3.14, add the module to the staging tree:

```sh
mkdir -p stage/usr/lib/python3.14/site-packages/ereader_hal
install -m 0644 python/ereader_hal/*.py stage/usr/lib/python3.14/site-packages/ereader_hal/
```

Adjust the destination for the Python version and layout used by your package.
Arrange the target's library search path so the bindings can load
`libereader-hal.so`, or set `EREADER_HAL_LIBRARY` to its installed absolute path.

## Versions and dependencies

Pin a source revision when building a release. Use the version in
[python/pyproject.toml](../python/pyproject.toml) for package metadata, and increase
your packaging revision when changing an existing package.

Keep the Python module, C headers, and library from the same source revision;
the ctypes structures must match the C ABI. Rebuild C consumers when a public
structure layout changes. Consumer packages should declare their HAL dependency
and any minimum version their API usage requires.

The C build links the system realtime, dynamic-loading, and math libraries.
Package dependencies must match the target's C runtime and firmware ABI.
Firmware services and device interfaces remain necessary for hardware controls.

Declare the [MIT license](../LICENSE) in package metadata and include its full
text with the distributed files.
