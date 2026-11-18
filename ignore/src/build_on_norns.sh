#!/bin/sh
# Build the RIFT-NORNS UGens on the norns itself (used when the prebuilt 32-bit plugin doesn't fit,
# e.g. a 64-bit image). Usage: sh build_on_norns.sh <install dir>
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
dest="${1:-$HOME/.local/share/SuperCollider/Extensions/RiftNorns}"
mkdir -p "$dest"
g++ -std=c++17 -O2 -fPIC -shared -ffp-contract=off -fno-exceptions -fno-rtti -fvisibility=hidden \
  -I"$here/include/plugin_interface" -I"$here/include/common" -I"$here" \
  "$here/RiftNorns.cpp" -o "$dest/RiftNorns.so"
echo "built $dest/RiftNorns.so"
