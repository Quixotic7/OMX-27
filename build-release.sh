#!/usr/bin/env bash
#
# Build OMX-27 release firmware for every platform and collect the artifacts,
# renamed, into .pio/build/release/:
#
#   OMX-27-<version>-RP2040.uf2   (pico     / RP2040 V3)
#   OMX-27-<version>-T4.hex       (teensy40 / Teensy 4.0 V2)
#   OMX-27-<version>-T31.hex      (teensy31 / Teensy 3.1/3.2 V1)
#
# Usage:
#   ./build-release.sh              # version auto-read from config.h
#   ./build-release.sh 1.15.2     # explicit version
#   CLEAN=1 ./build-release.sh      # wipe build dirs first (reproducible release build)
#
# NOTE: builds whatever is currently checked out -- check out your release
# commit/tag first.

set -euo pipefail

# Run from the repo root (this script lives there).
cd "$(dirname "$0")"

CONFIG="OMX-27-firmware/src/config.h"

# --- locate PlatformIO ---
PIO="$(command -v pio 2>/dev/null || true)"
[ -z "$PIO" ] && PIO="$HOME/.platformio/penv/bin/pio"
[ -x "$PIO" ] || { echo "ERROR: pio not found (looked on PATH and $HOME/.platformio/penv/bin/pio)"; exit 1; }

# --- version: explicit arg wins, else derive from config.h ---
if [ "$#" -ge 1 ]; then
	VERSION="$1"
else
	num() { sed -n "s/.*$1[^0-9]*\([0-9][0-9]*\).*/\1/p" "$CONFIG" | head -1; }
	VERSION="$(num MAJOR_VERSION).$(num MINOR_VERSION).$(num POINT_VERSION)"
fi

case "$VERSION" in
	[0-9]*.[0-9]*.[0-9]*) ;;
	*) echo "ERROR: could not determine a valid version (got '$VERSION')"; exit 1 ;;
esac

echo "==> OMX-27 release v${VERSION}  ($(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?') @ $(git rev-parse --short HEAD 2>/dev/null || echo '?'))"

# env : release-label : artifact extension
BUILDS="pico:RP2040:uf2 teensy40:T4:hex teensy31:T31:hex"

OUT=".pio/build/release"
mkdir -p "$OUT"

for entry in $BUILDS; do
	ENV="${entry%%:*}"; rest="${entry#*:}"
	LABEL="${rest%%:*}"; EXT="${rest##*:}"

	echo "==> [$ENV] building..."
	[ "${CLEAN:-0}" = "1" ] && "$PIO" run -e "$ENV" -t clean >/dev/null 2>&1 || true
	"$PIO" run -e "$ENV"

	SRC=".pio/build/$ENV/firmware.$EXT"
	DST="$OUT/OMX-27-${VERSION}-${LABEL}.${EXT}"
	[ -f "$SRC" ] || { echo "ERROR: expected artifact not found: $SRC"; exit 1; }
	cp "$SRC" "$DST"
	echo "    -> $DST"
done

echo
echo "==> Release artifacts in $OUT/:"
ls -lh "$OUT"/OMX-27-"${VERSION}"-* | awk '{printf "    %-8s %s\n", $5, $NF}'
