#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"

# KESTREL_ANDROID_NO_GAME_FILES=1 builds without an installed game, as CI does: the APK then draws its text with
# the free fonts below and leaves the game's HTML menu icons out.
packs=${KESTREL_ANDROID_RESOURCE_PACKS:-}
if [ -z "$packs" ] && [ -n "${KESTREL_VANILLA_PACK:-}" ]; then
    packs=$(dirname "$KESTREL_VANILLA_PACK")
fi
if [ "${KESTREL_ANDROID_NO_GAME_FILES:-0}" = "1" ]; then
    packs=
elif [ -z "$packs" ] || [ ! -d "$packs/vanilla" ]; then
    echo "Set KESTREL_ANDROID_RESOURCE_PACKS to an installed game's resource_packs directory," >&2
    echo "or KESTREL_ANDROID_NO_GAME_FILES=1 to build with free fonts only" >&2
    exit 1
fi

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

# The app downloads the rest of the vanilla pack from Mojang on first launch; only what that download lacks
# ships inside the APK, next to an index the app reads, since APK assets cannot be listed from native code.
rm -rf "$assets"
bundle=$assets/bundle
mkdir -p "$bundle/resource_packs/vanilla/ui" "$bundle/fonts"
cp data/ui/touch_controls.json "$bundle/resource_packs/vanilla/ui/kestrel_touch_controls.json"
cp "$fonts"/* "$bundle/fonts/"
if [ -n "$packs" ]; then
    mkdir -p "$bundle/resource_packs/vanilla/__brarchive"
    cp -R "$packs/vanilla/font" "$bundle/resource_packs/vanilla/"
    cp "$packs/vanilla/__brarchive/font.brarchive" "$bundle/resource_packs/vanilla/__brarchive/"
    data=$(dirname "$packs")
    if [ -d "$data/gui/dist/hbui" ]; then
        mkdir -p "$bundle/gui/dist"
        cp -R "$data/gui/dist/hbui" "$bundle/gui/dist/"
    fi
fi
{
    echo "# $(git rev-parse HEAD 2>/dev/null || date +%s)"
    (cd "$bundle" && find . -type f ! -name .DS_Store | sed 's|^\./||' | sort)
} > "$assets/kestrel_bundle.txt"

gradle=${KESTREL_GRADLE:-gradle}
"$gradle" -p android assembleRelease "-PkestrelSdl=$sdl" "-PkestrelAssets=$assets"
echo "APK: android/app/build/outputs/apk/release/app-release.apk"
