#!/bin/sh
cd "$(dirname "$0")"
../../glyph build life.glyph /tmp/life_dummy 2>/dev/null
cat x11_wrapper.c /tmp/glyph_out.c > /tmp/life_full.c
cc -w -Wno-error -Wno-int-conversion -Wno-incompatible-pointer-types -O2 /tmp/life_full.c -o life -no-pie -lX11
echo "Built life"
