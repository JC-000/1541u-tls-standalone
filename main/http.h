/*
 * http.h
 *
 * The plaintext a case receives, as an HTTP/1.0 response: status line, Content-Length, and the body's length and
 * SHA-256, so a case can check that every byte arrived.
 */

#ifndef HTTP_H_
#define HTTP_H_

#include <stdbool.h>
#include <stdint.h>
#include "psa/crypto.h"

typedef struct {
    char status[64];          // the status line, printable
    char head[12288];         // the header block, until the empty line
    int head_len;
    bool in_body;
    bool head_overflow;
    long content_length;      // -1: none sent
    uint32_t body_bytes;
    uint8_t sha256[32];
    psa_hash_operation_t op;
} http_t;

void http_init(http_t *h);
void http_feed(http_t *h, const uint8_t *p, int n);
void http_finish(http_t *h);

#endif /* HTTP_H_ */
