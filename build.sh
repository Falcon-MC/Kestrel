#!/usr/bin/env sh
set -e

cd "$(dirname "$0")"

LOG=build.txt
: > "$LOG"

for tool in cmake ninja c++; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "[ERROR] $tool not found in PATH."
        exit 1
    fi
done

echo "[1/2] Configuring project with CMake..."
if ! cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >> "$LOG" 2>&1; then
    echo "[ERROR] CMake configuration failed. See $LOG for details."
    cat "$LOG"
    exit 1
fi

echo "[2/2] Building Kestrel..."
if ! cmake --build build >> "$LOG" 2>&1; then
    cat "$LOG"
    echo "[ERROR] Build failed. Full output saved to $LOG."
    exit 1
fi

echo
echo "Build succeeded: build/Kestrel"
echo "Full build log saved to $LOG"
