#!/usr/bin/env bash
# Builds hidpoll (sysmodule), hidpoll_ctl (homebrew), and hidpoll_ovl (Ultrahand
# overlay), then stages a ready-to-copy SD layout under ./out/.
#
#   out/atmosphere/contents/420000000048504C/exefs.nsp
#   out/atmosphere/contents/420000000048504C/flags/boot2.flag
#   out/switch/hidpoll_ctl.nro
#   out/switch/.overlays/hidpoll_ovl.ovl
#   out/config/hidpoll/config.ini            (defaults: hid_hz=200, usb_hz=0)
#
# Copy the contents of out/ to the root of your SD card.
#
# libultrahand is fetched (shallow clone) into
# frontends/overlay/hidpoll_ovl/lib/libultrahand on first build. Requires
# switch-curl / switch-zlib / switch-mbedtls from (dkp-)pacman for the overlay
# build (libminizip ships inside switch-zlib).

set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO

if [ ! -d "$DEVKITPRO" ]; then
    echo "error: DEVKITPRO not found at $DEVKITPRO" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/out"
TID="420000000048504C"

SYSMOD_SRC="$ROOT/sysmodule/hidpoll"
HB_SRC="$ROOT/frontends/homebrew/hidpoll_ctl"
OVL_SRC="$ROOT/frontends/overlay/hidpoll_ovl"

SYSMOD_DIR="$OUT/atmosphere/contents/$TID"
FLAGS_DIR="$SYSMOD_DIR/flags"
SWITCH_DIR="$OUT/switch"
OVL_DIR="$OUT/switch/.overlays"
CONFIG_DIR="$OUT/config/hidpoll"

ULTRAHAND_DIR="$OVL_SRC/lib/libultrahand"
ULTRAHAND_URL="https://github.com/ppkantorski/libultrahand.git"

JOBS="$(nproc 2>/dev/null || echo 4)"

clean=0
for arg in "$@"; do
    case "$arg" in
        clean|-c|--clean) clean=1 ;;
        -h|--help)
            sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

if [ "$clean" -eq 1 ]; then
    echo ":: clean"
    make -C "$SYSMOD_SRC" clean >/dev/null || true
    make -C "$HB_SRC"     clean >/dev/null || true
    [ -d "$OVL_SRC" ] && make -C "$OVL_SRC" clean >/dev/null || true
    rm -rf "$OUT"
    exit 0
fi

echo ":: build hidpoll (sysmodule)"
make -C "$SYSMOD_SRC" -j"$JOBS"

echo ":: build hidpoll_ctl (homebrew)"
make -C "$HB_SRC" -j"$JOBS"

if [ ! -d "$ULTRAHAND_DIR/.git" ]; then
    echo ":: fetch libultrahand"
    mkdir -p "$(dirname "$ULTRAHAND_DIR")"
    git clone --depth 1 "$ULTRAHAND_URL" "$ULTRAHAND_DIR"
fi

echo ":: build hidpoll_ovl (Ultrahand overlay)"
make -C "$OVL_SRC" -j"$JOBS"

echo ":: stage -> $OUT"
rm -rf "$OUT"
mkdir -p "$FLAGS_DIR" "$SWITCH_DIR" "$OVL_DIR" "$CONFIG_DIR"

cp "$SYSMOD_SRC/hidpoll.nsp"    "$SYSMOD_DIR/exefs.nsp"
: > "$FLAGS_DIR/boot2.flag"
cp "$HB_SRC/hidpoll_ctl.nro"    "$SWITCH_DIR/hidpoll_ctl.nro"
cp "$OVL_SRC/hidpoll_ovl.ovl"   "$OVL_DIR/hidpoll_ovl.ovl"

cat > "$CONFIG_DIR/config.ini" <<'EOF'
hid_hz=200
usb_hz=0
EOF

echo ":: done"
find "$OUT" -type f -printf '   %p\n'
