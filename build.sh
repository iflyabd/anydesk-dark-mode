#!/bin/sh
# Build the AnyDesk dark-mode LD_PRELOAD shim.
#
#   ./build.sh            build ./libanydesk-dark.so
#   ./build.sh --install  build and install to /usr/local/lib
set -eu
cd "$(dirname "$0")"

OUT=libanydesk-dark.so

gcc -shared -fPIC -O2 -o "$OUT" src/addark.c \
    $(pkg-config --cflags --libs gtk+-3.0) -ldl -lm

echo "built $(pwd)/$OUT"

if [ "${1:-}" = "--install" ]; then
    install -Dm755 "$OUT" /usr/local/lib/libanydesk-dark.so
    echo "installed /usr/local/lib/libanydesk-dark.so"
fi
