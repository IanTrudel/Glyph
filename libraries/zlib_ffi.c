/*
 * zlib_ffi.c — zlib compression/decompression for Glyph
 * Prepended before Glyph runtime via cc_prepend.
 * Link with -lz.
 */

#include <stdlib.h>
#include <stdint.h>
#include <zlib.h>
typedef intptr_t GVal;

/*
 * Glyph bytes buffer layout: GVal hdr[3] = {(GVal)data_ptr, len, cap}
 * Same as array header but data is unsigned char*.
 */

/* zlib_compress bytes -> bytes (zlib-wrapped deflate) */
GVal zlib_compress(GVal b) {
    GVal *hdr = (GVal*)b;
    unsigned char *src = (unsigned char*)hdr[0];
    uLong src_len = (uLong)hdr[1];
    uLong dest_len = compressBound(src_len);

    unsigned char *dest = (unsigned char*)malloc(dest_len);
    int rc = compress(dest, &dest_len, src, src_len);
    if (rc != Z_OK) {
        free(dest);
        /* Return empty bytes */
        GVal *out = (GVal*)malloc(3 * sizeof(GVal));
        out[0] = (GVal)malloc(1);
        out[1] = 0;
        out[2] = 0;
        return (GVal)out;
    }

    GVal *out = (GVal*)malloc(3 * sizeof(GVal));
    out[0] = (GVal)dest;
    out[1] = (GVal)dest_len;
    out[2] = (GVal)dest_len;
    return (GVal)out;
}

/* zlib_uncompress bytes orig_size -> bytes */
GVal zlib_uncompress(GVal b, GVal orig_size) {
    GVal *hdr = (GVal*)b;
    unsigned char *src = (unsigned char*)hdr[0];
    uLong src_len = (uLong)hdr[1];
    uLong dest_len = (uLong)orig_size;

    unsigned char *dest = (unsigned char*)malloc(dest_len);
    int rc = uncompress(dest, &dest_len, src, src_len);
    if (rc != Z_OK) {
        free(dest);
        GVal *out = (GVal*)malloc(3 * sizeof(GVal));
        out[0] = (GVal)malloc(1);
        out[1] = 0;
        out[2] = 0;
        return (GVal)out;
    }

    GVal *out = (GVal*)malloc(3 * sizeof(GVal));
    out[0] = (GVal)dest;
    out[1] = (GVal)dest_len;
    out[2] = (GVal)dest_len;
    return (GVal)out;
}

/* zlib_compress_level bytes level -> bytes */
GVal zlib_compress_level(GVal b, GVal level) {
    GVal *hdr = (GVal*)b;
    unsigned char *src = (unsigned char*)hdr[0];
    uLong src_len = (uLong)hdr[1];
    uLong dest_len = compressBound(src_len);

    unsigned char *dest = (unsigned char*)malloc(dest_len);
    int rc = compress2(dest, &dest_len, src, src_len, (int)level);
    if (rc != Z_OK) {
        free(dest);
        GVal *out = (GVal*)malloc(3 * sizeof(GVal));
        out[0] = (GVal)malloc(1);
        out[1] = 0;
        out[2] = 0;
        return (GVal)out;
    }

    GVal *out = (GVal*)malloc(3 * sizeof(GVal));
    out[0] = (GVal)dest;
    out[1] = (GVal)dest_len;
    out[2] = (GVal)dest_len;
    return (GVal)out;
}
