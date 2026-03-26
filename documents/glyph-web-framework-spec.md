# Glyph Web Framework Specification

**Status:** Implemented
**Date:** 2026-03-25
**Libraries:** `libraries/web.glyph` + `libraries/web_ffi.c` + `libraries/json.glyph`

---

## 1. Philosophy & Design Goals

Glyph is an LLM-native language. Only LLMs write Glyph programs. This framework optimizes for:

**Token minimality.** Every API name, every pattern, every parameter is chosen to minimize BPE token count. An LLM building a CRUD API should need fewer than 200 tokens of framework boilerplate.

**Compositionality.** The framework is a bag of combinable functions, not a class hierarchy. Routes are data (arrays of records). Middleware is function composition. Handlers are plain functions. There is nothing to subclass, nothing to inherit, nothing to instantiate.

**Declarative where possible.** Route tables are declared, not imperatively registered. Middleware stacks are lists. Configuration is a record. The framework does the plumbing; the LLM writes only the business logic.

**Clear errors.** When something goes wrong, the error message must contain enough information for an LLM to fix the problem from text output alone. Errors name the handler, the route, and the HTTP status.

**Single-threaded, synchronous.** Glyph has no async. The server processes one request at a time via a blocking accept loop. This is correct for LLM tool-use scenarios (MCP, function calling) where requests arrive sequentially.

### 1.1 Non-Goals

- Concurrent request handling
- WebSocket support
- HTML templating (JSON API only)
- Database ORM
- Session/cookie management
- HTTPS (use a reverse proxy)

---

## 2. Architecture Overview

### 2.1 Library Dependency Stack

```
Layer 4: User application (app.glyph)
           |
           +-- glyph use libraries/web.glyph
           |
Layer 3: Web framework (libraries/web.glyph)
           |
           +-- glyph use libraries/json.glyph
           +-- glyph use libraries/network.glyph
           +-- glyph use libraries/stdlib.glyph
           |
Layer 2: JSON library (libraries/json.glyph)    [extracted from glyph.glyph]
         Network library (libraries/network.glyph)
         Standard library (libraries/stdlib.glyph)
           |
Layer 1: C runtime (glyph_alloc, str_concat, array_push, hm_*, sb_*, ...)
Layer 0: POSIX (sockets, stdio)
```

### 2.2 File Inventory

| File | Description |
|------|-------------|
| `libraries/json.glyph` | **NEW.** JSON parser/builder/generator extracted from glyph.glyph (~47 defs) |
| `libraries/network.glyph` | Existing: POSIX HTTP server FFI (15 fn, 8 extern). Needs meta key + extern fixes. |
| `libraries/network_ffi.c` | Existing: POSIX socket + HTTP/1.1 parser. Needs app singletons removed. |
| `libraries/web.glyph` | **NEW.** Framework library (~40 fn defs, 2 type defs) |
| `libraries/web_ffi.c` | **NEW.** State management + CORS response FFI (~60 lines) |
| `libraries/stdlib.glyph` | Existing: `map`, `filter`, `fold`, `join`, etc. No changes needed. |

### 2.3 Library Registration

The `glyph use` system does NOT follow transitive dependencies. Applications register all dependencies explicitly:

```sh
glyph use app.glyph libraries/stdlib.glyph
glyph use app.glyph libraries/json.glyph
glyph use app.glyph libraries/network.glyph
glyph use app.glyph libraries/web.glyph
```

This is correct for LLM users: four `glyph use` commands are explicit, debuggable, and cost ~40 tokens. The LLM can inspect registrations via `glyph libs app.glyph`.

---

## 3. Prerequisite: JSON Library Extraction

The compiler (`glyph.glyph`) contains a complete JSON subsystem that must be extracted into `libraries/json.glyph` so user programs can parse and build JSON.

### 3.1 Definitions to Extract (47 total)

**Type (1):**
- `JNode = {items:[I] keys:[S] nval:I sval:S tag:I}`

**Constants (6):**
- `jn_null = 0`, `jn_bool = 1`, `jn_int = 2`, `jn_str = 3`, `jn_array = 4`, `jn_object = 5`

**Infrastructure (2):**
- `mk_jnode` — constructor
- `json_pool_push` — append node to pool, return index

**Tokenizer (5):**
- `json_tokenize`, `json_tok_loop`, `json_tok_one`, `json_tok_one2`, `json_skip_ws`

**String handling (4):**
- `json_scan_str`, `json_scan_num`, `json_unescape`, `json_unesc_loop`

**Parser (7):**
- `json_parse`, `json_parse_str`, `json_parse_num`, `json_parse_obj`, `json_parse_obj_loop`, `json_parse_arr`, `json_parse_arr_loop`

**Getters (6):**
- `json_get`, `json_get_loop`, `json_get_str`, `json_get_int`, `json_arr_get`, `json_arr_len`

**Builder (8):**
- `jb_obj`, `jb_arr`, `jb_str`, `jb_int`, `jb_bool`, `jb_null`, `jb_put`, `jb_push`

**Generator (6):**
- `json_gen`, `json_gen_str`, `json_gen_str_loop`, `json_gen_arr`, `json_gen_arr_loop`, `json_gen_obj`, `json_gen_obj_loop`

### 3.2 Convenience Wrappers (2 new)

```glyph
json_decode src =
  pool = []
  tokens = json_tokenize(src)
  result = json_parse(src, tokens, 0, pool)
  {jd_pool: pool, jd_root: result.node}

json_encode pool idx = json_gen(pool, idx)
```

These reduce the parse/generate workflow from 4 lines to 1.

### 3.3 Extraction Process

1. `glyph init libraries/json.glyph`
2. Copy each definition verbatim from `glyph.glyph` via `glyph get` + `glyph put`
3. No externs needed — pure Glyph (depends only on runtime: `str_len`, `str_slice`, `str_char_at`, `array_push`, `sb_new`, `sb_append`, `sb_build`, `int_to_str`)
4. No `cc_prepend` or `cc_args` needed

---

## 4. Prerequisite: network.glyph Fixes

### 4.1 Add Meta Keys

Set `cc_prepend` so `glyph use` automatically includes the FFI C file:

```sql
INSERT INTO meta (key, value) VALUES ('cc_prepend', 'network_ffi.c');
```

### 4.2 Register Externs

The 8 network externs must exist in network.glyph's own `extern_` table (currently they only exist in linked app databases):

```
net_listen    net_listen    "I -> I"
net_accept    net_accept    "I -> I"
net_req_method net_req_method "I -> S"
net_req_path  net_req_path  "I -> S"
net_req_query net_req_query "I -> S"
net_req_body  net_req_body  "I -> S"
net_respond   net_respond   "I -> I -> S -> S -> I"
net_close     net_close     "I -> I"
```

### 4.3 Remove App Singletons from network_ffi.c

Remove these application-specific functions from `network_ffi.c` (lines 210-247):
- `api_get_items` — singleton items array
- `api_get_next_id` — auto-increment counter
- `api_swap_array` — array header copy

These belong in application code, not the networking library. The existing `examples/api/api.glyph` already has its own copy via `glyph link`, so removing them from the library source does not break the existing example.

---

## 5. Routing

### 5.1 Route Definition

A route is a record with method, path pattern, and handler function:

```glyph
web_route method path handler = {rhandler: handler, rmethod: method, rpath: path}
```

Shorthand constructors:

```glyph
web_get path handler = web_route("GET", path, handler)
web_post path handler = web_route("POST", path, handler)
web_put path handler = web_route("PUT", path, handler)
web_del path handler = web_route("DELETE", path, handler)
```

### 5.2 Route Table

Routes are a plain array. **Important:** handler references must be wrapped in lambdas to create proper closure structs. Raw function references (e.g., `handle_list`) are bare pointers and will crash when the dispatch loop uses the closure calling convention to invoke them.

```glyph
routes u = [
  web_get("/users", \req -> handle_list(req)),
  web_post("/users", \req -> handle_create(req)),
  web_get("/users/:id", \req -> handle_get(req)),
  web_put("/users/:id", \req -> handle_update(req)),
  web_del("/users/:id", \req -> handle_delete(req)),
]
```

### 5.3 Path Parameters

Path patterns support `:name` parameter segments. The router splits both pattern and actual path on `/`, walks segments pairwise. A segment starting with `:` (char code 58) is a parameter; its value is stored in a hashmap on the request context.

```glyph
web_split_path path =
  stripped = match str_char_at(path, 0) == 47
    true -> str_slice(path, 1, str_len(path))
    _ -> path
  match str_len(stripped) == 0
    true -> []
    _ -> str_split(stripped, "/")

web_match_route pattern path =
  pat_segs = web_split_path(pattern)
  req_segs = web_split_path(path)
  match array_len(pat_segs) == array_len(req_segs)
    true -> web_match_segs(pat_segs, req_segs, 0, hm_new())
    _ -> Err("no match")

web_match_segs pat_segs req_segs i params =
  match i >= array_len(pat_segs)
    true -> Ok(params)
    _ ->
      p = pat_segs[i]
      r = req_segs[i]
      match str_char_at(p, 0) == 58
        true ->
          key = str_slice(p, 1, str_len(p))
          _ = hm_set(params, key, r)
          web_match_segs(pat_segs, req_segs, i + 1, params)
        _ -> match str_eq(p, r)
          true -> web_match_segs(pat_segs, req_segs, i + 1, params)
          _ -> Err("no match")
```

Examples:
- `/users/:id` matches `/users/42` → `params{"id"} = "42"`
- `/users/:id/posts/:pid` matches `/users/3/posts/7` → `params{"id"} = "3"`, `params{"pid"} = "7"`

### 5.4 Route Dispatch

```glyph
web_dispatch routes req =
  web_dispatch_loop(routes, req, req.wmethod, req.wpath, 0)

web_dispatch_loop routes req method path i =
  match i >= array_len(routes)
    true -> web_not_found(req)
    _ ->
      rt = routes[i]
      match str_eq(rt.rmethod, method)
        true ->
          match web_match_route(rt.rpath, path)
            Ok(params) -> rt.rhandler(req{wparams: params})
            _ -> web_dispatch_loop(routes, req, method, path, i + 1)
        _ -> web_dispatch_loop(routes, req, method, path, i + 1)
```

Method mismatch falls through to the next route. All routes exhausted → 404.

---

## 6. Request/Response Model

### 6.1 Request Record

```glyph
WebReq = {wbody: S, whandle: I, wmethod: S, wparams: I, wpath: S, wquery: S}
```

- `whandle` — raw FFI request pointer (passed to `net_respond`)
- `wmethod` — HTTP method ("GET", "POST", etc.)
- `wpath` — URL path ("/users/42")
- `wquery` — query string ("page=1&limit=10")
- `wbody` — request body
- `wparams` — hashmap of path parameters (populated by router)

Construction from raw request:

```glyph
web_make_req handle =
  {wbody: net_req_body(handle),
   whandle: handle,
   wmethod: net_req_method(handle),
   wparams: hm_new(),
   wpath: net_req_path(handle),
   wquery: net_req_query(handle)}
```

### 6.2 Parameter Access

```glyph
web_param req key = hm_get(req.wparams, key)
web_param_int req key = str_to_int(hm_get(req.wparams, key))
```

### 6.3 Query String Parsing

```glyph
web_query req key = web_query_get(req.wquery, key)

web_query_get qs key =
  target = key + "="
  idx = str_index_of(qs, target)
  match idx < 0
    true -> ""
    _ ->
      start = idx + str_len(target)
      amp = str_index_of(str_slice(qs, start, str_len(qs)), "&")
      match amp < 0
        true -> str_slice(qs, start, str_len(qs))
        _ -> str_slice(qs, start, start + amp)
```

### 6.4 Response Helpers

```glyph
web_ok req body = net_respond(req.whandle, 200, "application/json", body)
web_created req body = net_respond(req.whandle, 201, "application/json", body)
web_no_content req = net_respond(req.whandle, 204, "application/json", "")
web_bad_request req msg = net_respond(req.whandle, 400, "application/json", web_err_json(msg))
web_not_found req = net_respond(req.whandle, 404, "application/json", web_err_json("not found"))
web_method_na req = net_respond(req.whandle, 405, "application/json", web_err_json("method not allowed"))
web_error req msg = net_respond(req.whandle, 500, "application/json", web_err_json(msg))
```

Error JSON helper (uses JSON builder for proper escaping):

```glyph
web_err_json msg =
  pool = []
  obj = jb_obj(pool)
  _ = jb_put(pool, obj, "error", jb_str(pool, msg))
  json_gen(pool, obj)
```

---

## 7. JSON Integration

### 7.1 Request Body Parsing

```glyph
web_json req =
  body = req.wbody
  match str_len(body) == 0
    true -> Err("empty body")
    _ -> Ok(json_decode(body))
```

Usage in a handler:

```glyph
handle_create req =
  match web_json(req)
    Ok(j) ->
      name = json_get_str(j.jd_pool, j.jd_root, "name")
      value = json_get_int(j.jd_pool, j.jd_root, "value")
      -- ... create resource ...
      web_created(req, response_json)
    Err(msg) -> web_bad_request(req, msg)
```

### 7.2 Response Body Building

The framework re-exports the JSON builder API from `json.glyph` without wrapping it. LLMs use `jb_obj`, `jb_put`, `jb_str`, `jb_int`, `json_gen` directly:

```glyph
handle_get req =
  pool = []
  obj = jb_obj(pool)
  _ = jb_put(pool, obj, "id", jb_int(pool, 1))
  _ = jb_put(pool, obj, "name", jb_str(pool, "widget"))
  web_ok(req, json_gen(pool, obj))
```

---

## 8. Middleware

### 8.1 Concept

Middleware is a function that takes a handler and returns a new handler:

```
middleware : (WebReq -> I) -> (WebReq -> I)
```

A function that takes a closure and returns a closure.

### 8.2 Implementation Pattern

Glyph only supports single-line lambdas (`\x -> expr`). Multi-line middleware bodies require a helper function:

```glyph
web_log_handle handler req =
  _ = eprintln(req.wmethod + " " + req.wpath)
  handler(req)

web_log handler = \req -> web_log_handle(handler, req)
```

**Note:** Use explicit string concatenation (`+`) for request fields, not string interpolation. The type checker may infer record fields as integers, causing interpolation to call `int_to_str` on string values.

### 8.3 CORS Middleware

CORS requires writing additional HTTP headers. Since `net_respond` controls header output at the C level, this requires a `web_respond_cors` FFI function in `web_ffi.c`:

```glyph
web_cors_handle handler req =
  match req.wmethod == "OPTIONS"
    true -> web_cors_preflight(req)
    _ -> handler(req)

web_cors handler = \req -> web_cors_handle(handler, req)

web_cors_preflight req =
  web_respond_cors(req.whandle, 204, "", "", "*")
```

### 8.4 Error Handling Middleware

For handlers that return `Result` types:

```glyph
web_catch_handle handler req =
  match handler(req)
    Err(msg) -> web_error(req, msg)
    Ok(v) -> v

web_catch handler = \req -> web_catch_handle(handler, req)
```

### 8.5 Middleware Composition

Middleware is composed by direct function application (no `fold` or arrays of function references — these trigger the closure calling convention issue):

```glyph
handler = web_log(web_cors(web_app(routes(0))))
```

The `web_app` helper wraps a route table into a dispatch handler:

```glyph
web_app routes = \req -> web_dispatch(routes, req)
```

Middleware applies outside-in: `web_log` wraps `web_cors` which wraps the dispatch handler.

---

## 9. State Management

### 9.1 The Problem

Glyph has no mutable global state at the language level. Zero-argument functions are not memoized. Persistent state requires a C FFI singleton.

The current API example uses three ad-hoc singletons in `network_ffi.c`. This is ugly and application-specific.

### 9.2 Generic State Store (web_ffi.c)

A self-contained key-value store in C. Uses a simple linked list (not Glyph's `hm_*`) to avoid symbol ordering issues with `cc_prepend`:

```c
/* web_ffi.c — Web framework FFI */

typedef struct _WebKV {
    char* key;
    GVal val;
    struct _WebKV* next;
} WebKV;

static WebKV* _web_store = NULL;

/* web_store_get key — retrieve value by string key, returns 0 if absent */
GVal web_store_get(GVal key) {
    const char* k = (const char*)(*(long long*)key);
    long long klen = *(long long*)((char*)key + 8);
    for (WebKV* cur = _web_store; cur; cur = cur->next) {
        if (strlen(cur->key) == (size_t)klen && memcmp(cur->key, k, klen) == 0)
            return cur->val;
    }
    return 0;
}

/* web_store_set key val — set value by string key, returns val */
GVal web_store_set(GVal key, GVal val) {
    const char* k = (const char*)(*(long long*)key);
    long long klen = *(long long*)((char*)key + 8);
    for (WebKV* cur = _web_store; cur; cur = cur->next) {
        if (strlen(cur->key) == (size_t)klen && memcmp(cur->key, k, klen) == 0) {
            cur->val = val;
            return val;
        }
    }
    WebKV* node = (WebKV*)malloc(sizeof(WebKV));
    node->key = (char*)malloc(klen + 1);
    memcpy(node->key, k, klen);
    node->key[klen] = '\0';
    node->val = val;
    node->next = _web_store;
    _web_store = node;
    return val;
}
```

**Externs in web.glyph:**

```
web_store_get  web_store_get  "S -> I"
web_store_set  web_store_set  "S -> I -> I"
web_respond_cors  web_respond_cors  "I -> I -> S -> S -> S -> I"
```

### 9.3 Glyph-Level State API

```glyph
web_state_get key = web_store_get(key)
web_state_set key val = web_store_set(key, val)

web_state_init key val =
  match web_store_get(key) == 0
    true -> web_store_set(key, val)
    _ -> web_store_get(key)
```

### 9.4 Auto-Increment ID

```glyph
web_next_id key =
  current = web_state_get(key)
  next = match current == 0
    true -> 1
    _ -> current + 1
  _ = web_state_set(key, next)
  next
```

### 9.5 Collection Helpers

```glyph
web_collection_init name = web_state_init(name, [])
web_collection_get name = web_state_get(name)
web_collection_add name item =
  items = web_state_get(name)
  _ = array_push(items, item)
  item

web_collection_find name pred =
  web_cfind_loop(web_state_get(name), pred, 0)

web_cfind_loop items pred i =
  match i >= array_len(items)
    true -> Err("not found")
    _ -> match pred(items[i])
      true -> Ok(items[i])
      _ -> web_cfind_loop(items, pred, i + 1)

web_collection_remove name idx =
  items = web_state_get(name)
  n = array_len(items)
  new_arr = []
  _ = web_cremove_loop(items, new_arr, idx, 0, n)
  _ = web_state_set(name, new_arr)
  0

web_cremove_loop items new_arr skip i n =
  match i >= n
    true -> 0
    _ ->
      _ = match i == skip
        true -> 0
        _ -> array_push(new_arr, items[i])
      web_cremove_loop(items, new_arr, skip, i + 1, n)
```

---

## 10. Error Handling

### 10.1 Validation Helpers

```glyph
web_require_body req =
  match str_len(req.wbody) > 0
    true -> Ok(req.wbody)
    _ -> Err("request body required")

web_require_json req =
  match str_len(req.wbody) == 0
    true -> Err("request body required")
    _ -> web_json(req)

web_require_field pool root key =
  val = json_get_str(pool, root, key)
  match str_len(val) > 0
    true -> Ok(val)
    _ -> Err("missing field: " + key)
```

### 10.2 Result-to-Response Mapping

```glyph
web_handle_result req result =
  match result
    Ok(body) -> web_ok(req, body)
    Err(msg) -> web_bad_request(req, msg)
```

---

## 11. Server Lifecycle

### 11.1 Configuration

```glyph
web_config port = {wc_port: port}
web_default_config u = web_config(8080)
```

### 11.2 Server Entry Point

`web_serve` takes a config and a pre-composed handler (not routes + middlewares):

```glyph
web_serve cfg handler =
  server = net_listen(cfg.wc_port)
  match server < 0
    true ->
      _ = eprintln("Failed to bind port")
      1
    _ ->
      _ = eprintln("Listening on :" + int_to_str(cfg.wc_port))
      web_loop(server, handler)

web_loop server handler =
  raw = net_accept(server)
  match raw == 0
    true -> web_loop(server, handler)
    _ ->
      req = web_make_req(raw)
      _ = handler(req)
      web_loop(server, handler)
```

### 11.3 Minimal Server

```glyph
main =
  web_serve(web_default_config(0), web_app(routes(0)))
```

### 11.4 Server with Middleware

```glyph
main =
  handler = web_log(web_app(routes(0)))
  web_serve(web_default_config(0), handler)
```

---

## 12. Example: Items API Rewritten

The existing `examples/api/` application rewritten with the framework.

### 12.1 Setup

```sh
glyph init app.glyph
glyph use app.glyph libraries/stdlib.glyph
glyph use app.glyph libraries/json.glyph
glyph use app.glyph libraries/network.glyph
glyph use app.glyph libraries/web.glyph
```

### 12.2 Application Definitions

```glyph
init_app u =
  _ = web_collection_init("items")
  _ = web_state_set("next_id", 0)
  0

item_json pool id name value =
  obj = jb_obj(pool)
  _ = jb_put(pool, obj, "id", jb_int(pool, id))
  _ = jb_put(pool, obj, "name", jb_str(pool, name))
  _ = jb_put(pool, obj, "value", jb_int(pool, value))
  obj

handle_list req =
  items = web_collection_get("items")
  web_ok(req, "[" + join(",", items) + "]")

handle_create req =
  match web_require_json(req)
    Ok(j) ->
      name = json_get_str(j.jd_pool, j.jd_root, "name")
      value = json_get_int(j.jd_pool, j.jd_root, "value")
      id = web_next_id("next_id")
      pool = []
      item = item_json(pool, id, name, value)
      json_str = json_gen(pool, item)
      _ = web_collection_add("items", json_str)
      web_created(req, json_str)
    Err(msg) -> web_bad_request(req, msg)

handle_get req =
  id_str = web_param(req, "id")
  target = "\"id\":" + id_str
  items = web_collection_get("items")
  idx = find_index(\item -> str_index_of(item, target) >= 0, items)
  match idx >= 0
    true -> web_ok(req, items[idx])
    _ -> web_not_found(req)

handle_update req =
  id_str = web_param(req, "id")
  target = "\"id\":" + id_str
  items = web_collection_get("items")
  idx = find_index(\item -> str_index_of(item, target) >= 0, items)
  match idx >= 0
    true ->
      match web_require_json(req)
        Ok(j) ->
          name = json_get_str(j.jd_pool, j.jd_root, "name")
          value = json_get_int(j.jd_pool, j.jd_root, "value")
          pool = []
          item = item_json(pool, str_to_int(id_str), name, value)
          json_str = json_gen(pool, item)
          _ = array_set(items, idx, json_str)
          web_ok(req, json_str)
        Err(msg) -> web_bad_request(req, msg)
    _ -> web_not_found(req)

handle_delete req =
  id_str = web_param(req, "id")
  target = "\"id\":" + id_str
  items = web_collection_get("items")
  idx = find_index(\item -> str_index_of(item, target) >= 0, items)
  match idx >= 0
    true ->
      pool = []
      obj = jb_obj(pool)
      _ = jb_put(pool, obj, "deleted", jb_int(pool, str_to_int(id_str)))
      _ = web_collection_remove("items", idx)
      web_ok(req, json_gen(pool, obj))
    _ -> web_not_found(req)

routes u = [
  web_get("/items", \req -> handle_list(req)),
  web_post("/items", \req -> handle_create(req)),
  web_get("/items/:id", \req -> handle_get(req)),
  web_put("/items/:id", \req -> handle_update(req)),
  web_del("/items/:id", \req -> handle_delete(req)),
]

main =
  _ = init_app(0)
  handler = web_log(web_app(routes(0)))
  web_serve(web_default_config(0), handler)
```

### 12.3 Build & Run

```sh
glyph build app.glyph app
./app
# Listening on :8080
```

No `build.sh`. No `sed`. No `cat`. The `glyph use` + `cc_prepend`/`cc_args` meta keys handle everything.

### 12.4 Comparison

| Metric | Current (`examples/api/`) | With framework |
|--------|--------------------------|----------------|
| App definitions | ~30 fn | 9 fn |
| Routing | Nested match chains (3 levels deep) | Declarative route table |
| JSON building | Manual string concat (`net_json_*`) | Structured builder (`jb_*`) |
| JSON parsing | Manual string scanning (`api_str_field`) | Pool-based parser (`json_get_str`) |
| State management | 3 ad-hoc FFI singletons | Generic key-value store |
| Build process | Shell script with `sed` + `cat` | `glyph build` (just works) |

---

## 13. Definition Inventory

### 13.1 json.glyph (49 definitions)

| Prefix | Count | Purpose |
|--------|-------|---------|
| `jn_*` | 6 fn | Tag constants |
| `jb_*` | 8 fn | Builder API |
| `json_*` | 31 fn | Tokenizer, parser, getters, generator |
| `mk_jnode` | 1 fn | Node constructor |
| `json_decode`, `json_encode` | 2 fn | Convenience wrappers |
| `JNode` | 1 type | Node record type |

### 13.2 web.glyph (60 definitions: 50 fn + 9 test + 1 type)

| Category | Definitions |
|----------|-------------|
| Routing (5) | `web_route`, `web_get`, `web_post`, `web_put`, `web_del` |
| Path matching (3) | `web_split_path`, `web_match_route`, `web_match_segs` |
| Dispatch (3) | `web_dispatch`, `web_dispatch_loop`, `web_app` |
| Request (5) | `web_make_req`, `web_param`, `web_param_int`, `web_query`, `web_query_get` |
| Response (8) | `web_ok`, `web_created`, `web_no_content`, `web_bad_request`, `web_not_found`, `web_method_na`, `web_error`, `web_err_json` |
| JSON (1) | `web_json` |
| Middleware (7) | `web_log`, `web_log_handle`, `web_cors`, `web_cors_handle`, `web_cors_preflight`, `web_catch`, `web_catch_handle` |
| State (4) | `web_state_get`, `web_state_set`, `web_state_init`, `web_next_id` |
| Collections (7) | `web_collection_init`, `web_collection_get`, `web_collection_add`, `web_collection_find`, `web_cfind_loop`, `web_collection_remove`, `web_cremove_loop` |
| Validation (4) | `web_require_body`, `web_require_json`, `web_require_field`, `web_handle_result` |
| Server (4) | `web_config`, `web_default_config`, `web_serve`, `web_loop` |
| Externs (3) | `web_store_get`, `web_store_set`, `web_respond_cors` |
| Tests (9) | `test_split_path`, `test_match_route`, `test_match_exact`, `test_query_get`, `test_route_constructors`, `test_err_json`, `test_config`, `test_state`, `test_next_id` |
| Types (1) | `WebReq` |

### 13.3 web_ffi.c (~60 lines C)

- `WebKV` struct + linked list
- `web_store_get` — key-value lookup
- `web_store_set` — key-value insert/update
- `web_respond_cors` — HTTP response with CORS headers

---

## 14. Implementation Phases

### Phase 1: JSON Library Extraction
Create `libraries/json.glyph`. Extract 47 defs from `glyph.glyph`. Add `json_decode`/`json_encode`. Write tests. **No C code.**

### Phase 2: network.glyph Cleanup
Add meta keys. Register externs in `extern_` table. Remove app singletons from `network_ffi.c`.

### Phase 3: web_ffi.c
Create `libraries/web_ffi.c` with state store + CORS response writer. ~60 lines C.

### Phase 4: web.glyph Core
Create `libraries/web.glyph`. Set meta keys. Register externs. Implement all ~42 definitions in order: request/response model, path matching, routing, dispatch, JSON integration, state, middleware, server lifecycle.

### Phase 5: Testing
Write tests for path splitting, route matching, query parsing, state store, collections.

### Phase 6: Example Application
Create `examples/web-api/` with rewritten items API. Verify `glyph build` works without shell scripts. Test all endpoints with `curl`.

---

## 15. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| `cc_prepend` ordering: `web_ffi.c` prepended before runtime, can't call `glyph_hm_*` | State store uses self-contained C linked list (no dependency on Glyph runtime) |
| No transitive `glyph use` resolution | Flat registration: app explicitly registers all 4 libs |
| JSON def names conflict with compiler when building `glyph.glyph` | json.glyph is never a lib_dep of glyph.glyph — no collision |
| Closure-based handlers cause memory growth | Boehm GC is integrated — closures are collected |
| Linear route scan too slow for many routes | LLM APIs typically have <20 endpoints; linear scan is fine |
| Closure calling convention: raw function pointers in arrays/records crash on indirect call | Wrap handler references in lambdas (`\req -> handler(req)`) to create proper closure structs |
| String interpolation infers record fields as integers | Use explicit string concatenation (`+`) for fields like `req.wmethod` instead of `"{req.wmethod}"` |
| Multi-line lambda bodies don't parse | Use helper function pattern: `web_log_handle handler req = ...` + `web_log handler = \req -> web_log_handle(handler, req)` |

---

## 16. Future Extensions (Out of Scope for v1)

- Transitive library resolution in `glyph use`
- `glyph init --web` template command
- 405 Method Not Allowed detection (path match without method match)
- Custom response headers API
- Static file serving
- Rate limiting middleware
- Request body size limits
- URL-encoded form body parsing
