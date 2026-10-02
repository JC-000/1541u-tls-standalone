/*
 * http.c
 *
 * See http.h.
 */

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "http.h"

void http_init(http_t *h)
{
    memset(h, 0, sizeof(*h));
    h->content_length = -1;
    h->op = psa_hash_operation_init();
    psa_hash_setup(&h->op, PSA_ALG_SHA_256);
}

static void parse_head(http_t *h)
{
    int n = 0;
    while ((n < h->head_len) && (n < (int)sizeof(h->status) - 1) && (h->head[n] != '\r') && (h->head[n] != '\n')) {
        char c = h->head[n];
        h->status[n] = (c >= 32 && c < 127 && c != '"') ? c : '?';
        n++;
    }
    h->status[n] = 0;
    for (char *line = strchr(h->head, '\n'); line; line = strchr(line + 1, '\n')) {
        if (!strncasecmp(line + 1, "Content-Length:", 15)) {
            h->content_length = strtol(line + 16, NULL, 10);
        }
    }
}

void http_feed(http_t *h, const uint8_t *p, int n)
{
    while (!h->in_body && (n > 0)) {
        if (h->head_len == (int)sizeof(h->head) - 1) {
            h->head_overflow = true;
            return;
        }
        h->head[h->head_len++] = (char)*p++;
        n--;
        if ((h->head_len >= 4) && !memcmp(h->head + h->head_len - 4, "\r\n\r\n", 4)) {
            h->head[h->head_len] = 0;
            h->in_body = true;
            parse_head(h);
        }
    }
    if (n > 0) {
        psa_hash_update(&h->op, p, n);
        h->body_bytes += n;
    }
}

void http_finish(http_t *h)
{
    size_t len = 0;
    psa_hash_finish(&h->op, h->sha256, sizeof(h->sha256), &len);
}
