# Items REST API

Demonstrates `glyph link` library composition with a POSIX HTTP/1.1 server.
The `net_*` definitions come from `libraries/network.glyph` (linked at DB creation time).

## Build & Run

```sh
cd examples/api
./build.sh
./api        # Listens on :8080
```

## Endpoints

```sh
# Service info
curl http://localhost:8080/

# List all items
curl http://localhost:8080/items

# Create an item
curl -X POST http://localhost:8080/items \
     -H 'Content-Type: application/json' \
     -d '{"name":"widget","value":42}'

# Get a specific item
curl http://localhost:8080/items/1

# Update an item
curl -X PUT http://localhost:8080/items/1 \
     -H 'Content-Type: application/json' \
     -d '{"name":"updated","value":99}'

# Delete an item
curl -X DELETE http://localhost:8080/items/1

# Not found
curl http://localhost:8080/items/999
```

## Architecture

```
libraries/network_ffi.c   — POSIX socket HTTP/1.1 server (C, ~160 lines)
libraries/network.glyph   — net_* externs + Glyph helpers (15 fn, 8 extern)
examples/api/api.glyph    — app code, pre-linked with network lib (45 fn, 8 extern)
examples/api/build.sh     — glyph build → cat ffi.c + generated.c → cc
```

The app uses `glyph link` to copy all `net_*` definitions from `network.glyph` into
`api.glyph` at DB creation time. No runtime library loading needed.
