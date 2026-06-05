#!/usr/bin/env sh
set -eu

if [ -z "${FSUAE_KICKSTART_FILE:-}" ]; then
  printf 'Set FSUAE_KICKSTART_FILE to your Kickstart 1.3 ROM path.\n' >&2
  exit 1
fi

if [ ! -f build/amigafits ]; then
  ./scripts/build.sh
fi

mkdir -p dist/AmigaFITS build
cp build/amigafits dist/AmigaFITS/amigafits
cp img.fits dist/AmigaFITS/img.fits

MOUNT_DIR="$(pwd)/dist/AmigaFITS"
CONFIG="build/amigafits.fs-uae"

{
  printf '[fs-uae]\n'
  printf 'amiga_model = A500\n'
  printf 'chip_memory = 512\n'
  printf 'slow_memory = 512\n'
  printf 'kickstart_file = %s\n' "$FSUAE_KICKSTART_FILE"
  if [ -n "${FSUAE_WORKBENCH_ADF:-}" ]; then
    printf 'floppy_drive_0 = %s\n' "$FSUAE_WORKBENCH_ADF"
  fi
  printf 'hard_drive_0 = %s\n' "$MOUNT_DIR"
} > "$CONFIG"

printf 'Generated %s\n' "$CONFIG"
printf 'Mounted Amiga directory: %s\n' "$MOUNT_DIR"

if [ "${FSUAE_NO_LAUNCH:-0}" = "1" ]; then
  exit 0
fi

fs-uae "$CONFIG"
