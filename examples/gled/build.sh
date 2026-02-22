#!/bin/sh
cd "$(dirname "$0")"
../../glyph build gled.glyph /tmp/gled_dummy 2>/dev/null
cat nc_wrapper.c /tmp/glyph_out.c > /tmp/gled_full.c
cc -w -Wno-error -Wno-int-conversion -Wno-incompatible-pointer-types -O2 /tmp/gled_full.c -o gled -no-pie -lncurses
echo "Built gled"
