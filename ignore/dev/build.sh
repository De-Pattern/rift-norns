#!/bin/sh
# Build the RIFT-NORNS UGens from ../src.
#   ignore/dev/build.sh host     this machine (for tests)       -> ignore/dev/build/host/RiftNorns.so
#   ignore/dev/build.sh armhf    norns (32-bit Raspberry Pi OS) -> ignore/RiftNorns/RiftNorns.so
# -ffp-contract=off and no fast-math keep the float32 maths in live.py's order (bit-exact).
set -eu
cd "$(dirname "$0")"
src=../src
inc="-I$src/include/plugin_interface -I$src/include/common -I$src"
flags="-std=c++17 -O2 -fPIC -shared -ffp-contract=off -fno-exceptions -fno-rtti -fvisibility=hidden -Wall"
case "${1:-host}" in
  host)  mkdir -p build/host; out=build/host/RiftNorns.so; g++ $flags $inc $src/RiftNorns.cpp -o $out ;;
  armhf) out=../RiftNorns/RiftNorns.so; arm-linux-gnueabihf-g++ $flags -march=armv7-a -mfpu=neon-vfpv4 \
           -mfloat-abi=hard -static-libgcc -static-libstdc++ $inc $src/RiftNorns.cpp -o $out ;;
  *) echo "usage: $0 host|armhf"; exit 1 ;;
esac
echo "built $out"
