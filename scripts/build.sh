#!/usr/bin/env sh
set -eu

IMAGE="vintagecomputingcarinthia/vbcc4vcc:latest"
OUT="build/amigafits"

mkdir -p build

docker run --rm --platform linux/amd64 \
  -v "$PWD:/work" \
  -w /work \
  "$IMAGE" \
  vc +kick13 -Iinclude -I/opt/NDK_3.9/Include/include_h -o "$OUT" \
    src/main.c \
    src/fits.c \
    src/viewer.c \
    -lmsoft -lamiga -lauto

printf 'Built %s\n' "$OUT"
