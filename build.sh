#!/bin/bash
set -e

echo -n "Looking for psp-cmake... "
command -v psp-cmake >/dev/null || { echo "not found"; exit 1; }
echo "found!"

test -n "$PSPDEV" || { echo "PSPDEV not set"; exit 1; }

echo "Building psp_m17"
mkdir -p build
cd build
psp-cmake .. && make -j$(nproc)

echo "All done."