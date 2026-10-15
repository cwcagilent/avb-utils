#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
printf 'int main(void) { return 0; }\n' > "$tmp/probe.c"
if ! "${CC:-cc}" -m32 "$tmp/probe.c" -o "$tmp/probe" > "$tmp/probe.log" 2>&1; then
    echo 'SKIP: no usable 32-bit compiler/linker libraries'
    cat "$tmp/probe.log"
    exit 77
fi
if ! "$tmp/probe"; then
    echo 'SKIP: host cannot execute 32-bit binaries'
    exit 77
fi
cmake -S "$src" -B "$tmp/build" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32
cmake --build "$tmp/build" --target avb_verify avb_verify_unit -j2
ctest --test-dir "$tmp/build" --output-on-failure -R '^(verify_unit|large_images)$'
