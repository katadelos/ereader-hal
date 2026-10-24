#!/bin/sh
set -eu
work_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
toolchain_root=${TOOLCHAIN_ROOT:-/Volumes/Dev/toolchain-home/x-tools}
toolchain=${TOOLCHAIN:-arm-kindle5-linux-gnueabi}
build_image=${BUILD_IMAGE:-koxtoolchain:build-env}
docker run --rm --platform linux/arm64 \
    -v "$work_dir:/work" -v "$toolchain_root:/opt/x-tools:ro" \
    -w /work "$build_image" make \
    CC="/opt/x-tools/$toolchain/bin/$toolchain-gcc" \
    AR="/opt/x-tools/$toolchain/bin/$toolchain-ar" \
    LDFLAGS='-s' "$@"
