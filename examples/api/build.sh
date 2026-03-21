#!/bin/sh
set -e
cd "$(dirname "$0")"

# Compile glyph → C (glyph's own cc will fail on missing net_* symbols, that's ok)
../../glyph build api.glyph api 2>/dev/null || true

# Insert forward declarations for net_* after "typedef intptr_t GVal;" so the
# extern wrappers (generated inline in glyph_out.c) can see their signatures.
sed '/typedef intptr_t GVal;/a GVal net_listen(GVal); GVal net_accept(GVal); GVal net_req_method(GVal); GVal net_req_path(GVal); GVal net_req_query(GVal); GVal net_req_body(GVal); GVal net_respond(GVal,GVal,GVal,GVal); GVal net_close(GVal); GVal api_get_items(GVal); GVal api_get_next_id(GVal); GVal api_swap_array(GVal,GVal);' \
  /tmp/glyph_out.c > /tmp/glyph_patched.c

# Concatenate patched glyph output + network FFI implementation
cat /tmp/glyph_patched.c ../../libraries/network_ffi.c > /tmp/api_full.c

cc -O2 -w -Wno-int-conversion -Wno-incompatible-pointer-types \
   /tmp/api_full.c -o api -no-pie -lm
echo "Built api — run with ./api"
