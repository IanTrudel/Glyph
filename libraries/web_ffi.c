/*
 * web_ffi.c — Web framework FFI for Glyph
 * Generic key-value state store + CORS response writer.
 * Prepended via cc_prepend before generated code, so we define GVal here.
 * Self-contained: no dependency on Glyph runtime symbols (hm_*, etc.)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
typedef intptr_t GVal;

/* --- Generic Key-Value State Store (linked list) --- */

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
        if ((long long)strlen(cur->key) == klen && memcmp(cur->key, k, klen) == 0)
            return cur->val;
    }
    return 0;
}

/* web_store_set key val — set value by string key, returns val */
GVal web_store_set(GVal key, GVal val) {
    const char* k = (const char*)(*(long long*)key);
    long long klen = *(long long*)((char*)key + 8);
    for (WebKV* cur = _web_store; cur; cur = cur->next) {
        if ((long long)strlen(cur->key) == klen && memcmp(cur->key, k, klen) == 0) {
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

/* --- CORS Response Writer --- */

/* Unpack a Glyph fat string {ptr, len} into C pointer + length */
static void _web_unpack_str(GVal gs, const char** out_ptr, long long* out_len) {
    *out_ptr = (const char*)(*(long long*)gs);
    *out_len = *(long long*)((char*)gs + 8);
}

/* web_respond_cors handle status content_type body origin
   Writes HTTP response with CORS headers, then closes fd and frees request. */
GVal web_respond_cors(GVal h, GVal status, GVal ct, GVal bd, GVal origin) {
    /* h is a NetRequest* (from network_ffi.c) — fd is at offset 0 */
    int fd = *(int*)h;

    const char* ct_ptr; long long ct_len;
    const char* bd_ptr; long long bd_len;
    const char* or_ptr; long long or_len;
    _web_unpack_str(ct, &ct_ptr, &ct_len);
    _web_unpack_str(bd, &bd_ptr, &bd_len);
    _web_unpack_str(origin, &or_ptr, &or_len);

    /* Status text */
    const char* status_text = "OK";
    if (status == 201) status_text = "Created";
    else if (status == 204) status_text = "No Content";
    else if (status == 400) status_text = "Bad Request";
    else if (status == 404) status_text = "Not Found";
    else if (status == 405) status_text = "Method Not Allowed";

    /* Build response with CORS headers */
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %lld %s\r\n"
        "Content-Type: %.*s\r\n"
        "Content-Length: %lld\r\n"
        "Access-Control-Allow-Origin: %.*s\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text,
        (int)ct_len, ct_ptr,
        bd_len,
        (int)or_len, or_ptr);

    write(fd, header, header_len);
    if (bd_len > 0) {
        write(fd, bd_ptr, (int)bd_len);
    }

    close(fd);
    free((void*)h);
    return 0;
}
