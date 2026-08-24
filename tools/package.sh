#!/usr/bin/env bash
# Builds the firmware and produces the two binary flavours that matter:
#
#   dist/lorascout-app.bin      raw app image  -> M5Launcher (SD/WebUI/OTA)
#   dist/lorascout-merged.bin   full image     -> M5Burner / esptool
#
# Usage: tools/package.sh [output-dir]
set -euo pipefail

NAME="lorascout"
ENVIRONMENT="cardputer-adv"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
BUILD="$ROOT/.pio/build/$ENVIRONMENT"

# The device has 8 MB of flash and no PSRAM (ESP32-S3FN8 on a Stamp-S3A).
FLASH_SIZE="8MB"
FLASH_MODE="qio"
FLASH_FREQ="80m"

# M5Launcher writes app images into an OTA partition, so the app must stay
# comfortably inside one. Fail the build rather than ship something the
# launcher would reject. 0x330000 is the app slot in default_8MB.csv.
MAX_APP_BYTES=$((3342336))

PIO="${PIO:-pio}"

cd "$ROOT"
"$PIO" run -e "$ENVIRONMENT"

APP="$BUILD/firmware.bin"
BOOTLOADER="$BUILD/bootloader.bin"
PARTITIONS="$BUILD/partitions.bin"

app_size=$(stat -c%s "$APP")
echo "app image: $app_size bytes (limit $MAX_APP_BYTES)"
if [ "$app_size" -gt "$MAX_APP_BYTES" ]; then
    echo "ERROR: app image will not fit an OTA partition" >&2
    exit 1
fi

# The app image must start with the ESP image magic or Launcher will not
# recognise it as an application binary.
magic=$(od -An -tx1 -N1 "$APP" | tr -d ' \n')
if [ "$magic" != "e9" ]; then
    echo "ERROR: app image magic is 0x$magic, expected 0xe9" >&2
    exit 1
fi

BOOT_APP0="$(find "$HOME/.platformio/packages/framework-arduinoespressif32" \
    -name boot_app0.bin 2>/dev/null | head -1)"
ESPTOOL="$(find "$HOME/.platformio/packages/tool-esptoolpy" \
    -maxdepth 2 -name "esptool.py" 2>/dev/null | head -1)"

# esptool.py imports pyserial, which lives in whichever environment PlatformIO
# was installed into -- not necessarily the system python3. Take the first
# interpreter that can actually import it rather than guessing.
pick_python() {
    local pio_bin candidate
    pio_bin="$(command -v "$PIO" 2>/dev/null || true)"
    for candidate in \
        "${PYTHON:-}" \
        "${pio_bin:+$(dirname "$pio_bin")/python}" \
        "$HOME/.platformio/penv/bin/python" \
        python3
    do
        [ -n "$candidate" ] || continue
        if "$candidate" -c "import serial" >/dev/null 2>&1; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

if ! PYTHON_BIN="$(pick_python)"; then
    echo "ERROR: no python with pyserial found; esptool cannot run" >&2
    echo "       set PYTHON=/path/to/python (the one PlatformIO runs under)" >&2
    exit 1
fi

mkdir -p "$OUT"
cp "$APP" "$OUT/$NAME-app.bin"

if [ -n "$ESPTOOL" ] && [ -n "$BOOT_APP0" ] && [ -f "$BOOT_APP0" ]; then
    "$PYTHON_BIN" "$ESPTOOL" --chip esp32s3 merge_bin \
        -o "$OUT/$NAME-merged.bin" \
        --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
        0x0 "$BOOTLOADER" \
        0x8000 "$PARTITIONS" \
        0xe000 "$BOOT_APP0" \
        0x10000 "$APP"
else
    echo "ERROR: esptool or boot_app0 not found under ~/.platformio" >&2
    exit 1
fi

echo
echo "artifacts in $OUT:"
ls -l "$OUT"
