#include "https_client.h"
#include <string.h>

static size_t bounded_length(const char *s, size_t maximum)
{
    size_t n = 0;
    if (!s) return maximum + 1;
    while (n <= maximum && s[n]) ++n;
    return n;
}

static bool alnum_ascii(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static bool token(unsigned char c)
{
    return alnum_ascii(c) || (c && strchr("!#$%&'*+-.^_`|~", c));
}

static bool equal_name(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char c = (unsigned char)*a++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)*b++) return false;
    }
    return *a == *b;
}

bool https_request_valid(const https_request *r)
{
    if (!r || !r->port || !r->timeout_ms || r->timeout_ms > HTTPS_MAX_TIMEOUT_MS ||
        r->body_length > HTTPS_MAX_REQUEST_BODY || (r->body_length && !r->body) ||
        r->header_count > HTTPS_MAX_HEADERS || (r->header_count && !r->headers)) return false;
    size_t n = bounded_length(r->host, HTTPS_MAX_HOST), label = 0;
    if (!n || n > HTTPS_MAX_HOST) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)r->host[i];
        if (c == '.') {
            if (!label || r->host[i - 1] == '-') return false;
            label = 0;
        } else {
            if ((!alnum_ascii(c) && c != '-') || (!label && c == '-') || ++label > 63) return false;
        }
    }
    if (!label || r->host[n - 1] == '-') return false;
    n = bounded_length(r->path, HTTPS_MAX_PATH);
    if (!n || n > HTTPS_MAX_PATH || r->path[0] != '/') return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)r->path[i];
        if (c <= 32 || c >= 127 || c == '#' || c == '\\') return false;
    }
    n = bounded_length(r->method, 16);
    if (!n || n > 16) return false;
    for (size_t i = 0; i < n; ++i) if (!token((unsigned char)r->method[i])) return false;
    /* CONNECT and Upgrade require a different lifecycle, outside this API. */
    if (equal_name(r->method, "connect")) return false;
    size_t total = 0;
    for (size_t i = 0; i < r->header_count; ++i) {
        const https_header *h = &r->headers[i];
        size_t name = bounded_length(h->name, HTTPS_MAX_HEADER_BYTES);
        size_t value = bounded_length(h->value, HTTPS_MAX_HEADER_BYTES);
        if (!name || name > HTTPS_MAX_HEADER_BYTES || value > HTTPS_MAX_HEADER_BYTES) return false;
        total += name + value + 4;
        if (total > HTTPS_MAX_HEADER_BYTES) return false;
        for (size_t j = 0; j < name; ++j) if (!token((unsigned char)h->name[j])) return false;
        for (size_t j = 0; j < value; ++j) {
            unsigned char c = (unsigned char)h->value[j];
            if (c < 32 || c == 127) return false;
        }
        if (equal_name(h->name, "host") || equal_name(h->name, "content-length") ||
            equal_name(h->name, "transfer-encoding") || equal_name(h->name, "connection") ||
            equal_name(h->name, "upgrade") || equal_name(h->name, "expect")) return false;
    }
    return true;
}

static https_status budget(const https_request *r, const https_transport *t,
                           int64_t start, uint32_t *remaining)
{
    if (r->cancelled && r->cancelled(r->cancel_context)) return HTTPS_CANCELLED;
    int64_t now = t->now_ms(t->context);
    if (now < start || now - start >= r->timeout_ms) return HTTPS_TIMEOUT;
    *remaining = r->timeout_ms - (uint32_t)(now - start);
    return HTTPS_OK;
}

https_status https_fetch(const https_request *r, https_response *out, const https_transport *t)
{
    if (!out) return HTTPS_INVALID_ARGUMENT;
    out->length = 0;
    out->http_status = 0;
    if (!https_request_valid(r) || out->capacity > HTTPS_MAX_RESPONSE_BODY ||
        (out->capacity && !out->body) || !t || !t->now_ms || !t->open ||
        !t->read || !t->complete || !t->end) return HTTPS_INVALID_ARGUMENT;
    int64_t start = t->now_ms(t->context), expected = -1;
    if (start < 0) return HTTPS_INVALID_ARGUMENT;
    uint32_t remaining;
    size_t used = 0;
    https_status result = budget(r, t, start, &remaining);
    if (result != HTTPS_OK) return result;
    https_status opened = t->open(t->context, r, remaining, &out->http_status, &expected);
    result = budget(r, t, start, &remaining);
    if (result != HTTPS_OK) goto done;
    if (opened != HTTPS_OK) { result = opened; goto done; }
    if (out->http_status < 200 || out->http_status > 599 || expected < -1) {
        result = HTTPS_PROTOCOL_ERROR; goto done;
    }
    /* HEAD and bodyless status codes may advertise a representation length. */
    bool bodyless = !strcmp(r->method, "HEAD") || out->http_status == 204 || out->http_status == 304;
    if (bodyless) expected = 0;
    if (expected > (int64_t)out->capacity) { result = HTTPS_BODY_TOO_LARGE; goto done; }
    for (;;) {
        result = budget(r, t, start, &remaining);
        if (result != HTTPS_OK) break;
        uint8_t overflow;
        size_t room = out->capacity - used;
        size_t chunk = room > 512 ? 512 : room;
        size_t offered = chunk ? chunk : 1;
        int n = t->read(t->context, chunk ? out->body + used : &overflow, offered, remaining);
        result = budget(r, t, start, &remaining);
        if (result != HTTPS_OK) break;
        if (n < 0) { result = HTTPS_TRANSPORT_ERROR; break; }
        if ((size_t)n > offered) { result = HTTPS_PROTOCOL_ERROR; break; }
        if (!n) {
            result = t->complete(t->context) && (expected < 0 || used == (size_t)expected)
                   ? HTTPS_OK : HTTPS_TRUNCATED;
            break;
        }
        if (bodyless || (expected >= 0 && used + (size_t)n > (size_t)expected)) {
            result = HTTPS_PROTOCOL_ERROR; break;
        }
        if (!room) { result = HTTPS_BODY_TOO_LARGE; break; }
        used += (size_t)n;
    }
done:
    t->end(t->context);
    if (result == HTTPS_OK) out->length = used;
    return result;
}
