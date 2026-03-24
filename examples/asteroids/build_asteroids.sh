#!/bin/sh
set -e
cd "$(dirname "$0")"
../../glyph build asteroids.glyph /tmp/ast_dummy 2>/dev/null
cat asteroids_ffi.c /tmp/glyph_out.c > /tmp/asteroids_full.c
cc -O2 -Wno-int-conversion -Wno-incompatible-pointer-types \
   /tmp/asteroids_full.c -o asteroids -lSDL2 -lm
echo "Built: ./asteroids"
