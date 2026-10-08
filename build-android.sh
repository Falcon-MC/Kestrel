#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"

packs=${KESTREL_ANDROID_RESOURCE_PACKS:-}
if [ -z "$packs" ] && [ -n "${KESTREL_VANILLA_PACK:-}" ]; then
    packs=$(dirname "$KESTREL_VANILLA_PACK")
fi
if [ -z "$packs" ] || [ ! -d "$packs/vanilla" ]; then
    echo "Set KESTREL_ANDROID_RESOURCE_PACKS to an installed game's resource_packs directory" >&2
    exit 1
fi

root=$(pwd)/build/android
sdl=$root/SDL
assets=$root/assets
if [ ! -d "$sdl" ]; then
    git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git "$sdl"
fi

# The app downloads the rest of the vanilla pack from Mojang on first launch; only what that download lacks
# ships inside the APK, next to an index the app reads, since APK assets cannot be listed from native code.
rm -rf "$assets"
bundle=$assets/bundle
mkdir -p "$bundle/resource_packs/vanilla/__brarchive" "$bundle/resource_packs/vanilla/ui" "$bundle/gui/dist"
cp -R "$packs/vanilla/font" "$bundle/resource_packs/vanilla/"
cp "$packs/vanilla/__brarchive/font.brarchive" "$bundle/resource_packs/vanilla/__brarchive/"
cp data/ui/touch_controls.json "$bundle/resource_packs/vanilla/ui/kestrel_touch_controls.json"
data=$(dirname "$packs")
if [ -d "$data/gui/dist/hbui" ]; then
    cp -R "$data/gui/dist/hbui" "$bundle/gui/dist/"
fi
{
    echo "# $(git rev-parse HEAD 2>/dev/null || date +%s)"
    (cd "$bundle" && find . -type f ! -name .DS_Store | sed 's|^\./||' | sort)
} > "$assets/kestrel_bundle.txt"

gradle=${KESTREL_GRADLE:-gradle}
"$gradle" -p android assembleRelease "-PkestrelSdl=$sdl" "-PkestrelAssets=$assets"
echo "APK: android/app/build/outputs/apk/release/app-release.apk"
