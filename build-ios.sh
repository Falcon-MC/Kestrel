#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"

certificates=${KESTREL_IOS_CA_BUNDLE:-/etc/ssl/cert.pem}

cmake -S . -B build/ios -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.3 \
    -DCMAKE_BUILD_TYPE=Release \
    "-DKESTREL_IOS_CA_BUNDLE=$certificates"
cmake --build build/ios --target KestrelIpa --parallel "${KESTREL_BUILD_JOBS:-8}"
