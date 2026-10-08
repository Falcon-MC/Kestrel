#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"

packs=${KESTREL_IOS_RESOURCE_PACKS:-}
if [ -z "$packs" ] && [ -n "${KESTREL_VANILLA_PACK:-}" ]; then
    packs=$(dirname "$KESTREL_VANILLA_PACK")
fi
if [ -z "$packs" ]; then
    packs="$HOME/Library/Application Support/BedrockOnMac/games/release/data/resource_packs"
fi
certificates=${KESTREL_IOS_CA_BUNDLE:-/etc/ssl/cert.pem}

cmake -S . -B build/ios -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.3 \
    -DCMAKE_BUILD_TYPE=Release \
    "-DKESTREL_IOS_RESOURCE_PACKS=$packs" \
    "-DKESTREL_IOS_CA_BUNDLE=$certificates"
cmake --build build/ios --target KestrelIpa --parallel "${KESTREL_BUILD_JOBS:-8}"
