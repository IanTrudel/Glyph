/*
 * network_ffi.c — POSIX HTTP/1.1 server for Glyph network library
 * No external dependencies beyond POSIX sockets.
 * Prepended via cc_prepend before generated code, so we define GVal here.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
typedef intptr_t GVal;

/* Glyph fat string: {long long ptr, long long len} on heap */
static GVal make_glyph_str(const char* s, int len) {
    char* buf = (char*)malloc(len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    long long* gs = (long long*)malloc(16);
    gs[0] = (long long)buf;
    gs[1] = (long long)len;
    return (GVal)gs;
}

typedef struct {
    int fd;
    char method[16];
    char path[512];
    char query[512];
    char body[16384];
} NetRequest;

/* net_listen port — bind + listen, return server fd or -1 */
GVal net_listen(GVal port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 16) < 0) { close(fd); return -1; }
    return (GVal)fd;
}

/* net_accept sfd — blocking accept + HTTP parse, returns heap ptr to NetRequest or 0 */
GVal net_accept(GVal sfd) {
    int cfd = accept((int)sfd, NULL, NULL);
    if (cfd < 0) return 0;

    /* Read request into buffer */
    char buf[20480];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int n = read(cfd, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        /* Check if headers complete */
        if (strstr(buf, "\r\n\r\n")) break;
    }
    buf[total] = '\0';

    NetRequest* req = (NetRequest*)calloc(1, sizeof(NetRequest));
    req->fd = cfd;

    /* Parse request line: METHOD /path?query HTTP/1.x */
    char* line_end = strstr(buf, "\r\n");
    if (!line_end) { free(req); close(cfd); return 0; }

    char request_line[1024];
    int line_len = (int)(line_end - buf);
    if (line_len >= (int)sizeof(request_line)) line_len = (int)sizeof(request_line) - 1;
    memcpy(request_line, buf, line_len);
    request_line[line_len] = '\0';

    /* Extract method */
    char* sp1 = strchr(request_line, ' ');
    if (!sp1) { free(req); close(cfd); return 0; }
    int mlen = (int)(sp1 - request_line);
    if (mlen >= (int)sizeof(req->method)) mlen = (int)sizeof(req->method) - 1;
    memcpy(req->method, request_line, mlen);
    req->method[mlen] = '\0';

    /* Extract path + query */
    char* path_start = sp1 + 1;
    char* sp2 = strchr(path_start, ' ');
    if (!sp2) { free(req); close(cfd); return 0; }
    char full_path[512];
    int plen = (int)(sp2 - path_start);
    if (plen >= (int)sizeof(full_path)) plen = (int)sizeof(full_path) - 1;
    memcpy(full_path, path_start, plen);
    full_path[plen] = '\0';

    char* q = strchr(full_path, '?');
    if (q) {
        int pathlen = (int)(q - full_path);
        if (pathlen >= (int)sizeof(req->path)) pathlen = (int)sizeof(req->path) - 1;
        memcpy(req->path, full_path, pathlen);
        req->path[pathlen] = '\0';
        strncpy(req->query, q + 1, sizeof(req->query) - 1);
    } else {
        strncpy(req->path, full_path, sizeof(req->path) - 1);
        req->query[0] = '\0';
    }

    /* Find body after \r\n\r\n */
    char* body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        /* Check Content-Length */
        char cl_header[64] = "Content-Length: ";
        char* cl = strcasestr(buf, cl_header);
        if (cl && cl < body_start) {
            int content_len = atoi(cl + strlen(cl_header));
            if (content_len > 0 && content_len < (int)sizeof(req->body)) {
                /* May need to read more */
                int already = (int)(buf + total - body_start);
                if (already < 0) already = 0;
                memcpy(req->body, body_start, already);
                int remaining = content_len - already;
                if (remaining > 0) {
                    int got = read(cfd, req->body + already, remaining);
                    if (got < 0) got = 0;
                    req->body[already + got] = '\0';
                } else {
                    req->body[already] = '\0';
                }
            }
        }
    }

    return (GVal)req;
}

/* net_req_method h — returns method as Glyph fat string */
GVal net_req_method(GVal h) {
    NetRequest* r = (NetRequest*)h;
    return make_glyph_str(r->method, (int)strlen(r->method));
}

/* net_req_path h — returns path as Glyph fat string */
GVal net_req_path(GVal h) {
    NetRequest* r = (NetRequest*)h;
    return make_glyph_str(r->path, (int)strlen(r->path));
}

/* net_req_query h — returns query string as Glyph fat string */
GVal net_req_query(GVal h) {
    NetRequest* r = (NetRequest*)h;
    return make_glyph_str(r->query, (int)strlen(r->query));
}

/* net_req_body h — returns body as Glyph fat string */
GVal net_req_body(GVal h) {
    NetRequest* r = (NetRequest*)h;
    return make_glyph_str(r->body, (int)strlen(r->body));
}

/* net_respond h status ct body — write HTTP response, close fd, free request struct */
GVal net_respond(GVal h, GVal status, GVal ct, GVal bd) {
    NetRequest* r = (NetRequest*)h;

    /* Unpack Glyph fat strings: {ptr: long long, len: long long} */
    char* ct_ptr = (char*)(*(long long*)ct);
    long long ct_len = *(long long*)(ct + 8);
    char* bd_ptr = (char*)(*(long long*)bd);
    long long bd_len = *(long long*)(bd + 8);

    /* Status text */
    const char* status_text = "OK";
    if (status == 201) status_text = "Created";
    else if (status == 204) status_text = "No Content";
    else if (status == 400) status_text = "Bad Request";
    else if (status == 404) status_text = "Not Found";
    else if (status == 405) status_text = "Method Not Allowed";
    else if (status == 500) status_text = "Internal Server Error";

    /* Build response header */
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %lld %s\r\n"
        "Content-Type: %.*s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text,
        (int)ct_len, ct_ptr,
        bd_len);

    write(r->fd, header, header_len);
    if (bd_len > 0) {
        write(r->fd, bd_ptr, (int)bd_len);
    }

    close(r->fd);
    free(r);
    return 0;
}

/* net_close fd — close a socket fd */
GVal net_close(GVal fd) {
    close((int)fd);
    return 0;
}

