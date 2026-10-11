#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"

# Minecraft resources are downloaded at runtime; never copy an installed game into the APK.
root=$(pwd)/build/android
sdl=$root/SDL
assets=$root/assets
fonts=$root/fonts
if [ ! -d "$sdl" ]; then
    git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git "$sdl"
fi

sha256() {
    if command -v sha256sum > /dev/null; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

mkdir -p "$fonts"
grep -v '^#' data/fallback_fonts.txt | while read -r name hash url; do
    [ -n "$name" ] || continue
    if [ ! -f "$fonts/$name" ] || [ "$(sha256 "$fonts/$name")" != "$hash" ]; then
        curl -fsSL -o "$fonts/$name.part" "$url"
        if [ "$(sha256 "$fonts/$name.part")" != "$hash" ]; then
            echo "$name does not match its pinned SHA-256" >&2
            exit 1
        fi
        mv "$fonts/$name.part" "$fonts/$name"
    fi
done

# The APK contains the project's touch layout and licensed free fonts; the Minecraft pack is downloaded
# on first launch. Android reads an index because APK assets cannot be listed from native code.
rm -rf "$assets"
bundle=$assets/bundle
mkdir -p "$bundle/resource_packs/vanilla/ui" "$bundle/fonts"
cp data/ui/touch_controls.json "$bundle/resource_packs/vanilla/ui/kestrel_touch_controls.json"
cp "$fonts"/* "$bundle/fonts/"

{
    echo "# $(git rev-parse HEAD 2>/dev/null || date +%s)"
    (cd "$bundle" && find . -type f ! -name .DS_Store | sed 's|^\./||' | sort)
} > "$assets/kestrel_bundle.txt"

gradle=${KESTREL_GRADLE:-gradle}
"$gradle" -p android assembleRelease "-PkestrelSdl=$sdl" "-PkestrelAssets=$assets"
echo "APK: android/app/build/outputs/apk/release/app-release.apk"
