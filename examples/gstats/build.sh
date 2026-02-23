#!/bin/sh
cd "$(dirname "$0")"

# Build: generate C, prepend FFI wrapper, compile
../../glyph build gstats.glyph gstats 2>/dev/null || true
cat gstats_ffi.c /tmp/glyph_out.c > /tmp/gstats_full.c
cc -O2 -Wno-int-conversion -Wno-incompatible-pointer-types -o gstats /tmp/gstats_full.c
echo "Built gstats"

# Test (if --test flag passed)
if [ "$1" = "--test" ]; then
  ../../glyph test gstats.glyph 2>/dev/null || true
  cat gstats_ffi.c /tmp/glyph_test.c > /tmp/gstats_test_full.c
  cc -O2 -Wno-int-conversion -Wno-incompatible-pointer-types -o /tmp/gstats_test /tmp/gstats_test_full.c
  /tmp/gstats_test
fi
