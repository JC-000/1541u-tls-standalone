// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * app.c
 *
 * 1541u-tls-standalone: the 1541 Ultimate's ESP32 TLS engine (tls_engine.c, compiled unmodified) with no Ultimate
 * code linked. FreeRTOS, lwIP and mbedTLS from ESP-IDF; this file plays the Ultimate's part. It owns the TCP
 * socket, sends each request in the bridge's RPC layout (rpc_calls.h), checks each STEP reply as the Ultimate's
 * wifi_tls_take_step() does, and pumps the session as network_target.cc's tls_handshake(), tls_read() and
 * tls_write() do (all in the engine commit, engine.env).
 *
 * Origin: take_step() is transcribed from wifi_tls_take_step() in software/io/wifi/wifi_tls.cc, and the pump is ported
 * from software/io/network/network_target.cc, of the 1541 Ultimate firmware (GideonZ/1541ultimate, GPL-3.0-or-later).
 * tools/verify_transcriptions.py checks take_step() against the engine commit and pins the pump's firmware source.
 *
 * Every case prints one RESULT line and one CASE line, PASS or FAIL against its expectation. The gate (VERDICT) is
 * the local-server cases plus bundle-wronghost; the other public hosts are informational, as they change by themselves.
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "mbedtls/build_info.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "engine_task.h"
#include "http.h"
#include "net.h"
#include "stubs.h"
#include "pins.h"
#include "blob.h"

#define APP_VERSION      "4"
#define CLIENT_STACK     16384
#define CT_ROOM          1536  // network_target.h:70,152 (NET_TLS_CT_MAX, tls_ct_out)
#define PT_ROOM          1024  // network_target.h:71,153 (NET_TLS_PT_MAX, tls_pt)
#define WRITE_BUDGET     4     // network_target.cc:70
#define HANDSHAKE_MS     30000
#define IDLE_MS          20000 // no byte from the server for this long ends a read
#define TOTAL_MS         240000
#define ANY              -1
#define CA_TAG           0x5a5a
#define WRITE_CHUNK      892   // plaintext per WRITE, about one UCI command block
#define STATUS_PORT      8450  // tools/app_server.py: how the client ended its last /keepalive

// How a session ended (conn_t.end); a success case lists the ends it accepts
enum {
    END_NONE = 0, END_NOTIFY = 1, END_FIN = 2, END_RX_ERR = 4, END_IDLE = 8, END_TIMEOUT = 16, END_FAILED = 32,
    END_ERROR = 64, END_US = 128,
};
static const char *end_name(int e)
{
    switch (e) {
    case END_NOTIFY:  return "close-notify";
    case END_FIN:     return "tcp-fin-no-notify";
    case END_RX_ERR:  return "rx-error";
    case END_IDLE:    return "idle-timeout";
    case END_TIMEOUT: return "read-timeout";
    case END_FAILED:  return "tls-failed";
    case END_ERROR:   return "error";
    case END_US:      return "closed-by-client";
    default:          return "none";
    }
}

// wifi_tls_take_step()'s results (wifi_tls.h)
enum { TAKE_OK = 0, TAKE_AGAIN = 1, TAKE_REFUSED = -1, TAKE_INVALID = -2, TAKE_FOREIGN = -3 };

enum { STUB_NONE, STUB_NO_ENTROPY, STUB_SNTP_TIME };
enum { EXPECT_OK, EXPECT_FAIL, EXPECT_REFUSED };

extern const uint8_t ca_der_start[] asm("_binary_ca_der_start");
extern const uint8_t ca_der_end[] asm("_binary_ca_der_end");

static const uint8_t pin_wrong[32] = { 0xde, 0xad, 0xbe, 0xef };
static char local_host[40] = "10.0.2.2"; // QEMU user networking: the host; NVS "local" overrides it

typedef struct {
    const char *name;
    bool gate;                // a local, deterministic case: counts for the VERDICT
    const char *host;         // NULL: the local server
    uint16_t port;
    const char *sni;
    const char *path;
    uint8_t trust;
    uint8_t opts;
    const uint8_t *pin;
    uint8_t time_source;      // what the OPEN carries
    uint8_t stub;
    bool http11;              // HTTP/1.1 (a server that refuses 1.0); the reply must then carry Content-Length
    uint32_t post_len;        // POST this many bytes of the blob (in WRITE_CHUNK writes) instead of a GET
    bool close_early;         // CLOSE while ESTABLISHED once the body is complete (END_US)
    bool peer_notify;         // EXPECT_OK: the server must report the client's close_notify (STATUS_PORT)
    // Expectations
    uint8_t expect;
    int ends;                 // EXPECT_OK: the accepted ends (default close_notify); any other needs Content-Length
    uint16_t version;         // EXPECT_OK: 0 any
    uint32_t body_len;        // EXPECT_OK: 0 = not known (Content-Length is checked whenever it is sent)
    const uint8_t *body_sha;  // EXPECT_OK: NULL = not known
    int time_source_used;     // EXPECT_OK: the STATS time_source (ANY: not checked)
    int open_reason;          // EXPECT_REFUSED
    int fail_reason;          // EXPECT_FAIL: ANY or tls_fail_reason_t
    uint32_t flags;           // EXPECT_FAIL: verify_flags bits that must be set
    bool flags_exact;         // EXPECT_FAIL: verify_flags must be exactly flags
    int alert_in;             // EXPECT_FAIL: ANY or the alert
} case_t;

typedef struct {
    const case_t *c;
    int fd;
    uint8_t slot;
    uint16_t tag;
    uint16_t seq;
    uint8_t held[CT_ROOM];
    int held_len;
    bool more;
    bool rx_fin;
    int rx_err;
    int quiet;
    int end;
    const char *error;        // with END_ERROR, or a handshake that did not finish
    int64_t read_deadline, idle_until;
    // What happened
    int open_ret;
    uint8_t open_reason;
    int esp_err;
    uint8_t state, fail_reason, alert_in, alert_out;
    uint32_t verify_flags;
    rpc_tls_stats_resp hs;    // STATS right after ESTABLISHED, as the Ultimate reads it
    bool established;
    int64_t t_open, t_established, t_req_sent, t_end;
    uint32_t ct_in, ct_out, steps, again_steps, step_us_max, hs_engine_us, xfer_engine_us;
    http_t http;
    uint8_t ct_buf[CT_ROOM];  // the Ultimate's tls_ct_out
    uint8_t pt_buf[PT_ROOM];  // the Ultimate's tls_pt
    uint16_t ct_len, pt_len;
    char peer[48];            // what the server saw at the end (peer_notify cases)
    command_buf_t buf;        // one RPC, request and reply
} conn_t;

static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}

static uint16_t next_tag(void)
{
    static uint16_t tag;
    return ++tag ? tag : ++tag;
}

static void *rpc_begin(conn_t *k, uint8_t command)
{
    memset(k->buf.data, 0, sizeof(rpc_header_t));
    rpc_header_t *h = (rpc_header_t *)k->buf.data;
    h->command = command;
    h->sequence = ++k->seq;
    return k->buf.data;
}

static bool send_all(conn_t *k, const uint8_t *p, int n)
{
    int64_t until = ms_now() + 5000;
    while (n > 0) {
        int s = send(k->fd, p, n, MSG_DONTWAIT);
        if (s > 0) {
            p += s;
            n -= s;
            continue;
        }
        if ((s < 0) && (errno != EWOULDBLOCK) && (errno != EAGAIN)) {
            return false;
        }
        if (ms_now() > until) {
            return false;
        }
        fd_set w;
        FD_ZERO(&w);
        FD_SET(k->fd, &w);
        struct timeval tv = { 0, 100000 };
        select(k->fd + 1, NULL, &w, NULL, &tv);
    }
    return true;
}

static bool wait_readable(int fd, int ms)
{
    fd_set r;
    FD_ZERO(&r);
    FD_SET(fd, &r);
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    return select(fd + 1, &r, NULL, NULL, &tv) > 0;
}

// wifi_tls_take_step() (wifi_tls.cc): the checks the Ultimate makes on every STEP reply
static int take_step(conn_t *k, uint8_t flags)
{
    const uint8_t *reply = k->buf.data;
    int size = k->buf.size;
    const rpc_tls_step_resp *result = (const rpc_tls_step_resp *)reply;
    const int header = offsetof(rpc_tls_step_resp, data);
    if ((size < (int)sizeof(rpc_header_t)) || (result->hdr.command != CMD_TLS_STEP) || (result->hdr.sequence != k->seq)) {
        return TAKE_FOREIGN;
    }
    if ((size == (int)sizeof(rpc_espcmd_resp)) && (result->esp_err != 0)) {
        k->esp_err = result->esp_err;
        return TAKE_REFUSED;
    }
    if ((size < header) || (size != header + result->ct_out_len + result->pt_out_len)) {
        return TAKE_INVALID;
    }
    bool has_state = result->state <= TLS_STATE_FAILED;
    bool failed = has_state && (result->state == TLS_STATE_FAILED);
    bool no_mem = has_state && (result->esp_err == ESP_ERR_NO_MEM);
    bool again = no_mem && ((result->state == TLS_STATE_HANDSHAKING) || (result->state == TLS_STATE_ESTABLISHED));
    bool closed = no_mem && (result->state == TLS_STATE_CLOSED);
    if ((result->esp_err != 0) && !failed && !again && !closed) {
        k->esp_err = result->esp_err;
        if (has_state) {
            k->state = result->state;
        }
        return TAKE_REFUSED;
    }
    if (!has_state) {
        return TAKE_INVALID;
    }
    if (!failed && (result->fail_reason != TLS_FAIL_NONE)) {
        return TAKE_INVALID;
    }
    uint16_t ct = result->ct_out_len;
    uint16_t pt = result->pt_out_len;
    if ((ct > CT_ROOM) || (pt > PT_ROOM)) {
        return TAKE_INVALID;
    }
    if ((flags & TLS_STEP_NO_READ) && (pt != 0)) {
        return TAKE_INVALID;
    }
    if (failed) {
        pt = 0;
    } else if ((pt != 0) && (result->state != TLS_STATE_ESTABLISHED) && (result->state != TLS_STATE_CLOSED)) {
        return TAKE_INVALID;
    }
    memcpy(k->ct_buf, result->data, ct);
    memcpy(k->pt_buf, result->data + result->ct_out_len, pt);
    k->ct_len = ct;
    k->pt_len = pt;
    k->state = result->state;
    k->esp_err = result->esp_err;
    k->alert_in = result->alert_in;
    k->alert_out = result->alert_out;
    k->fail_reason = result->fail_reason;
    k->verify_flags = result->verify_flags;
    return again ? TAKE_AGAIN : TAKE_OK;
}

// One CMD_TLS_STEP with the held ciphertext (and plaintext to write); its ciphertext goes out before it returns.
// Returns take_step()'s result; an error sets k->error.
static int step(conn_t *k, uint8_t flags, const uint8_t *pt, int pt_len, bool handshake)
{
    rpc_tls_step_req *req = rpc_begin(k, CMD_TLS_STEP);
    req->slot = k->slot;
    req->flags = flags;
    req->tag = k->tag;
    req->ct_in_len = k->held_len;
    req->pt_in_len = pt_len;
    memcpy(req->data, k->held, k->held_len);
    memcpy(req->data + k->held_len, pt, pt_len);
    k->buf.size = sizeof(rpc_tls_step_req) + k->held_len + pt_len;
    uint32_t us = 0;
    engine_call(&k->buf, &us);
    k->steps++;
    if (us > k->step_us_max) {
        k->step_us_max = us;
    }
    if (handshake) {
        k->hs_engine_us += us;
    } else {
        k->xfer_engine_us += us;
    }
    k->ct_len = k->pt_len = 0;
    int r = take_step(k, flags);
    if (r == TAKE_INVALID || r == TAKE_FOREIGN) {
        k->error = "contract-violation";
        return r;
    }
    if (r == TAKE_REFUSED) {
        k->error = "module-refused";
        return r;
    }
    if (r == TAKE_AGAIN) {
        k->again_steps++;
    } else {
        k->ct_in += k->held_len;
        k->held_len = 0;
    }
    k->ct_out += k->ct_len;
    if (k->ct_len && !send_all(k, k->ct_buf, k->ct_len) && (k->state != TLS_STATE_FAILED)) {
        k->error = "send-error";
        return TAKE_REFUSED;
    }
    return r;
}

static bool conn_connect(conn_t *k)
{
    const case_t *c = k->c;
    char port[8];
    snprintf(port, sizeof(port), "%u", c->port);
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *res = NULL;
    if ((getaddrinfo(c->host ? c->host : local_host, port, &hints, &res) != 0) || !res) {
        k->error = "dns-failed";
        return false;
    }
    k->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval tv = { 10, 0 };
    setsockopt(k->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int ok = connect(k->fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok) {
        k->error = "connect-failed";
        close(k->fd);
        k->fd = -1;
        return false;
    }
    int on = 1;
    fcntl(k->fd, F_SETFL, fcntl(k->fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(k->fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    return true;
}

// CMD_TLS_STATS; slot TLS_SLOT_GLOBAL with tag 0 for the clock and heap only
static bool stats(conn_t *k, uint8_t slot, uint16_t tag, rpc_tls_stats_resp *out)
{
    rpc_tls_session_req *req = rpc_begin(k, CMD_TLS_STATS);
    req->slot = slot;
    req->tag = tag;
    k->buf.size = sizeof(rpc_tls_session_req);
    uint32_t us;
    engine_call(&k->buf, &us);
    memcpy(out, k->buf.data, sizeof(*out));
    return (k->buf.size == sizeof(rpc_tls_stats_resp)) && (out->hdr.sequence == k->seq);
}

static void set_ca(conn_t *k)
{
    rpc_tls_set_ca_req *req = rpc_begin(k, CMD_TLS_SET_CA);
    int len = ca_der_end - ca_der_start;
    req->ca_tag = CA_TAG;
    req->der_len = len;
    memcpy(req->der, ca_der_start, len);
    k->buf.size = sizeof(rpc_tls_set_ca_req) + len;
    uint32_t us;
    engine_call(&k->buf, &us);
    rpc_tls_set_ca_resp *resp = (rpc_tls_set_ca_resp *)k->buf.data;
    if (resp->esp_err != 0) {
        printf("SET_CA failed: %d reject %u\n", resp->esp_err, resp->reject);
    }
}

static bool conn_open(conn_t *k, uint8_t slot)
{
    const case_t *c = k->c;
    k->slot = slot;
    k->tag = next_tag();
    k->fd = -1;
    k->state = TLS_STATE_HANDSHAKING;
    if (!conn_connect(k)) {
        return false;
    }
    int sni_len = c->sni ? strlen(c->sni) : 0;
    rpc_tls_open_req *req = rpc_begin(k, CMD_TLS_OPEN);
    req->trust = c->trust;
    req->sni_len = sni_len;
    req->slot = slot;
    req->time_source = c->time_source;
    req->unix_time = (c->time_source == TLS_TIME_NIOS) ? (uint32_t)time(NULL) : 0;
    req->tag = k->tag;
    req->opts = c->opts;
    req->ca_tag = (c->trust == TLS_TRUST_CUSTOM) ? CA_TAG : 0;
    if (c->pin) {
        memcpy(req->pin, c->pin, TLS_PIN_LEN);
    }
    memcpy(req->sni, c->sni, sni_len);
    k->buf.size = sizeof(rpc_tls_open_req) + sni_len;
    k->t_open = ms_now();
    uint32_t us;
    engine_call(&k->buf, &us);
    rpc_tls_open_resp *resp = (rpc_tls_open_resp *)k->buf.data;
    k->open_ret = resp->esp_err;
    k->open_reason = resp->reason;
    if ((k->buf.size != sizeof(rpc_tls_open_resp)) || (resp->hdr.sequence != k->seq)) {
        k->error = "contract-violation";
        return false;
    }
    if (k->open_ret != 0) {
        k->error = "open-refused";
        return false;
    }
    return true;
}

// network_target.cc tls_handshake(): the first step has no input and makes the ClientHello
static bool conn_handshake(conn_t *k)
{
    int64_t deadline = ms_now() + HANDSHAKE_MS;
    k->more = true;
    k->held_len = 0;
    while (true) {
        if (ms_now() > deadline) {
            k->error = "handshake-timeout";
            return false;
        }
        if ((k->held_len == 0) && !k->more) {
            if (!wait_readable(k->fd, 100)) {
                continue;
            }
            int n = recv(k->fd, k->held, CT_ROOM, MSG_DONTWAIT);
            if (n == 0) {
                k->error = "peer-closed";
                return false;
            }
            if (n < 0) {
                if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
                    continue;
                }
                k->error = "peer-reset";
                return false;
            }
            k->held_len = n;
        }
        int r = step(k, 0, NULL, 0, true);
        if ((r != TAKE_OK) && (r != TAKE_AGAIN)) {
            return false;
        }
        if (k->state == TLS_STATE_FAILED) {
            k->end = END_FAILED;
            return false;
        }
        if (k->state == TLS_STATE_CLOSED) {
            k->error = "peer-closed";
            return false;
        }
        if (k->state == TLS_STATE_ESTABLISHED) {
            http_feed(&k->http, k->pt_buf, k->pt_len);
            break;
        }
        k->more = (k->ct_len != 0) || (r == TAKE_AGAIN);
    }
    k->t_established = ms_now();
    if (!stats(k, k->slot, k->tag, &k->hs) || (k->hs.esp_err != 0) || (k->hs.group == 0)) {
        k->error = "stats-failed";
        return false;
    }
    k->established = true;
    return true;
}

// network_target.cc tls_write(): NO_READ steps until all of it is taken and nothing is left to send
static bool conn_write(conn_t *k, const uint8_t *data, int len)
{
    int pending = len;
    for (int steps = 0; steps < WRITE_BUDGET; steps++) {
        int r = step(k, TLS_STEP_NO_READ, data, pending, false);
        if ((r != TAKE_OK) && (r != TAKE_AGAIN)) {
            return false;
        }
        if (r == TAKE_OK) {
            pending = 0;
        }
        if (k->state == TLS_STATE_FAILED) {
            k->end = END_FAILED;
            return false;
        }
        if (k->state != TLS_STATE_ESTABLISHED) {
            k->error = "write-not-established";
            return false;
        }
        if ((pending == 0) && (k->ct_len == 0)) {
            return true;
        }
    }
    k->error = "write-budget";
    return false;
}

// Block i (32 B) of the blob: SHA-256 of i as 4 bytes big-endian (tools/mkblob.py)
static void blob_block(uint32_t i, uint8_t *out)
{
    uint8_t be[4] = { i >> 24, i >> 16, i >> 8, i };
    size_t n;
    psa_hash_compute(PSA_ALG_SHA_256, be, 4, out, 32, &n);
}

static bool conn_request(conn_t *k)
{
    const case_t *c = k->c;
    char req[256];
    int n;
    if (c->post_len) {
        n = snprintf(req, sizeof(req),
                     "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: 1541u-tls-standalone/" APP_VERSION
                     "\r\nContent-Length: %u\r\n\r\n", c->path, c->sni, (unsigned)c->post_len);
    } else {
        n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.%d\r\nHost: %s\r\nUser-Agent: 1541u-tls-standalone/" APP_VERSION
                     "\r\nAccept-Encoding: identity\r\nConnection: %s\r\n\r\n",
                     c->path, c->http11, c->sni ? c->sni : "localhost", c->close_early ? "keep-alive" : "close");
    }
    if (!conn_write(k, (const uint8_t *)req, n)) {
        return false;
    }
    static uint8_t chunk[WRITE_CHUNK + 32];
    for (uint32_t off = 0; off < c->post_len; ) {
        int len = 0;
        while ((len < WRITE_CHUNK) && (off + len < c->post_len)) {
            blob_block((off + len) / 32, chunk + len);
            len += 32;
        }
        if (!conn_write(k, chunk, len)) {
            return false;
        }
        off += len;
    }
    k->t_req_sent = ms_now();
    k->read_deadline = k->t_req_sent + TOTAL_MS;
    k->idle_until = k->t_req_sent + IDLE_MS;
    k->more = true;
    return true;
}

// One round of network_target.cc tls_read(): eager top-up, a step, the ciphertext out, the plaintext taken. Waits at
// most wait_ms when there is nothing to feed. Sets k->end and returns true when the stream has ended.
static bool conn_read_once(conn_t *k, int wait_ms)
{
    if (ms_now() > k->read_deadline) {
        k->end = END_TIMEOUT;
        return true;
    }
    if (!k->rx_err && !k->rx_fin && (k->held_len < CT_ROOM)) {
        int n = recv(k->fd, k->held + k->held_len, CT_ROOM - k->held_len, MSG_DONTWAIT);
        if (n > 0) {
            k->held_len += n;
            k->idle_until = ms_now() + IDLE_MS;
        } else if (n == 0) {
            k->rx_fin = true;
        } else if ((errno != EWOULDBLOCK) && (errno != EAGAIN)) {
            k->rx_err = errno;
        }
    }
    bool draining = k->rx_err || k->rx_fin;
    if ((k->held_len == 0) && !k->more && !draining) {
        if (ms_now() > k->idle_until) {
            k->end = END_IDLE;
            return true;
        }
        wait_readable(k->fd, wait_ms);
        return false;
    }
    int fed = k->held_len;
    int r = step(k, 0, NULL, 0, false);
    if ((r != TAKE_OK) && (r != TAKE_AGAIN)) {
        k->end = END_ERROR;
        return true;
    }
    k->more = (k->ct_len != 0) || (k->pt_len != 0) || (r == TAKE_AGAIN);
    if (k->state == TLS_STATE_FAILED) {
        k->end = END_FAILED;
        return true;
    }
    http_feed(&k->http, k->pt_buf, k->pt_len);
    if (k->state == TLS_STATE_CLOSED) {
        k->end = END_NOTIFY;
        return true;
    }
    const http_t *h = &k->http;
    if (k->c->close_early && h->in_body && (h->content_length >= 0) && (h->body_bytes >= (uint32_t)h->content_length)) {
        k->end = END_US;
        return true;
    }
    if (k->state != TLS_STATE_ESTABLISHED) {
        k->error = "module-state";
        k->end = END_ERROR;
        return true;
    }
    if (k->pt_len) {
        k->quiet = 0;
    } else if (draining && (fed == 0)) {
        if (++k->quiet >= 2) {
            k->end = k->rx_err ? END_RX_ERR : END_FIN;
            return true;
        }
    } else {
        k->quiet = 0;
    }
    return false;
}

static void conn_read_all(conn_t *k)
{
    while (!conn_read_once(k, 100)) {
    }
}

static void conn_close(conn_t *k)
{
    if (k->state == TLS_STATE_ESTABLISHED || k->state == TLS_STATE_HANDSHAKING) {
        rpc_tls_session_req *req = rpc_begin(k, CMD_TLS_CLOSE);
        req->slot = k->slot;
        req->tag = k->tag;
        k->buf.size = sizeof(rpc_tls_session_req);
        uint32_t us;
        engine_call(&k->buf, &us);
        rpc_tls_close_resp *resp = (rpc_tls_close_resp *)k->buf.data;
        if ((resp->esp_err == 0) && resp->ct_out_len && (k->fd >= 0)) {
            send_all(k, resp->data, resp->ct_out_len);
        }
    }
    if (k->fd >= 0) {
        close(k->fd);
        k->fd = -1;
    }
    http_finish(&k->http);
}

static bool judge(const conn_t *k, char *why, int len)
{
    const case_t *c = k->c;
    const http_t *h = &k->http;
    if (c->expect == EXPECT_REFUSED) {
        snprintf(why, len, "open %d reason %u", k->open_ret, k->open_reason);
        return (k->open_ret != 0) && (k->open_reason == c->open_reason);
    }
    if (c->expect == EXPECT_FAIL) {
        if ((k->state != TLS_STATE_FAILED) || k->established || (k->end != END_FAILED)) {
            snprintf(why, len, "expected a handshake failure, got %s/%s", end_name(k->end), k->error ? k->error : "-");
            return false;
        }
        if ((c->fail_reason != ANY) && (k->fail_reason != c->fail_reason)) {
            snprintf(why, len, "fail_reason %u, expected %d", k->fail_reason, c->fail_reason);
            return false;
        }
        if (c->flags_exact ? (k->verify_flags != c->flags) : ((k->verify_flags & c->flags) != c->flags)) {
            snprintf(why, len, "verify_flags 0x%08x, expected %s0x%08x", (unsigned)k->verify_flags,
                     c->flags_exact ? "exactly " : "the bits ", (unsigned)c->flags);
            return false;
        }
        if ((c->alert_in != ANY) && (k->alert_in != c->alert_in)) {
            snprintf(why, len, "alert_in %u, expected %d", k->alert_in, c->alert_in);
            return false;
        }
        snprintf(why, len, "fail_reason %u verify 0x%08x alert_in %u", k->fail_reason, (unsigned)k->verify_flags,
                 k->alert_in);
        return true;
    }
    int ends = c->ends ? c->ends : END_NOTIFY;
    if (!k->established || !(k->end & ends)) {
        snprintf(why, len, "end %s/%s not accepted", end_name(k->end), k->error ? k->error : "-");
        return false;
    }
    if (c->version && (k->hs.tls_version != c->version)) {
        snprintf(why, len, "version 0x%04x, expected 0x%04x", k->hs.tls_version, c->version);
        return false;
    }
    if ((c->time_source_used != ANY) && (k->hs.time_source != c->time_source_used)) {
        snprintf(why, len, "time_source %u, expected %d", k->hs.time_source, c->time_source_used);
        return false;
    }
    if (!h->in_body || h->head_overflow || strncmp(h->status, "HTTP/1.", 7) || !strstr(h->status, " 200 ")) {
        snprintf(why, len, "status \"%s\"%s", h->status, h->head_overflow ? ", header overflow" : "");
        return false;
    }
    if ((k->end != END_NOTIFY) && (h->content_length < 0)) {
        snprintf(why, len, "ended by %s without a Content-Length: completeness unknown", end_name(k->end));
        return false;
    }
    if ((h->content_length >= 0) && (h->body_bytes != (uint32_t)h->content_length)) {
        snprintf(why, len, "body %u B, Content-Length %ld", (unsigned)h->body_bytes, h->content_length);
        return false;
    }
    if (c->body_len && (h->body_bytes != c->body_len)) {
        snprintf(why, len, "body %u B, expected %u", (unsigned)h->body_bytes, (unsigned)c->body_len);
        return false;
    }
    if (c->body_sha && memcmp(h->sha256, c->body_sha, 32)) {
        snprintf(why, len, "body SHA-256 differs");
        return false;
    }
    if (c->peer_notify && strcmp(k->peer, "notify")) {
        snprintf(why, len, "the server saw no close_notify: %s", k->peer);
        return false;
    }
    snprintf(why, len, "%s, body %u B%s%s, %s%s", h->status, (unsigned)h->body_bytes,
             (h->content_length >= 0) ? " = Content-Length" : "", c->body_sha ? ", SHA-256 match" : "",
             end_name(k->end), c->peer_notify ? ", server saw close_notify" : "");
    return true;
}

static int gate_pass, gate_fail, gate_cases, info_pass, info_fail;
static int gate_unreachable; // gate failures that never reached the server (bundle-wronghost without a network)

static void report(conn_t *k, uint32_t heap_before, const rpc_tls_stats_resp *end, uint32_t stack_free,
                   uint32_t alloc_max)
{
    const case_t *c = k->c;
    const http_t *h = &k->http;
    char why[128];
    bool pass = judge(k, why, sizeof(why));
    if (c->gate) {
        pass ? gate_pass++ : gate_fail++;
        if (!pass && k->error && (!strcmp(k->error, "dns-failed") || !strcmp(k->error, "connect-failed"))) {
            gate_unreachable++;
        }
    } else {
        pass ? info_pass++ : info_fail++;
    }
    int64_t xfer_ms = (k->t_req_sent && k->t_end > k->t_req_sent) ? k->t_end - k->t_req_sent : 0;
    printf("RESULT case=%s gate=%d peer=\"%s\" host=%s port=%u sni=%s trust=%u opts=%u end=%s error=%s open_ret=%d "
           "open_reason=%u state=%u esp_err=%d fail_reason=%u verify=0x%08x alert_in=%u alert_out=%u "
           "version=0x%04x suite=0x%04x group=0x%04x ems=%u time_source=%u http=\"%s\" content_length=%ld "
           "body_bytes=%u body_sha256=%02x%02x%02x%02x%02x%02x%02x%02x ct_in=%u ct_out=%u "
           "hs_wall_ms=%lld hs_mbedtls_ms=%u.%03u hs_engine_ms=%u.%03u xfer_ms=%lld xfer_engine_ms=%u.%03u "
           "steps=%u again_steps=%u step_max_ms=%u.%03u heap_before=%u heap_min=%u heap_peak_use=%d "
           "largest_free=%u largest_alloc=%u stack_free_hs=%u stack_free=%u stack_size=%u\n",
           c->name, c->gate, k->peer, c->host ? c->host : local_host, c->port, c->sni ? c->sni : "-", c->trust, c->opts,
           end_name(k->end), k->error ? k->error : "-", k->open_ret, k->open_reason, k->state, k->esp_err,
           k->fail_reason, (unsigned)k->verify_flags, k->alert_in, k->alert_out, k->hs.tls_version,
           k->hs.ciphersuite, k->hs.group, k->hs.ems, k->hs.time_source, h->status, h->content_length,
           (unsigned)h->body_bytes, h->sha256[0], h->sha256[1], h->sha256[2], h->sha256[3], h->sha256[4],
           h->sha256[5], h->sha256[6], h->sha256[7], (unsigned)k->ct_in, (unsigned)k->ct_out,
           (long long)(k->established ? k->t_established - k->t_open : 0), (unsigned)k->hs.handshake_us / 1000,
           (unsigned)k->hs.handshake_us % 1000, (unsigned)k->hs_engine_us / 1000, (unsigned)k->hs_engine_us % 1000,
           (long long)xfer_ms, (unsigned)k->xfer_engine_us / 1000, (unsigned)k->xfer_engine_us % 1000,
           (unsigned)k->steps, (unsigned)k->again_steps, (unsigned)k->step_us_max / 1000,
           (unsigned)k->step_us_max % 1000, (unsigned)heap_before, (unsigned)end->heap_min,
           (int)(heap_before - end->heap_min), (unsigned)end->largest_block, (unsigned)alloc_max,
           (unsigned)k->hs.stack_free, (unsigned)stack_free, TLS_TASK_STACK);
    printf("CASE %s %s %s (%s)\n", c->name, c->gate ? "gate" : "info", pass ? "PASS" : "FAIL", why);
}

// tools/app_server.py's status port: "<connections ended> <how the last one ended>"
static int peer_status(char *result, int len)
{
    char port[8], line[96];
    snprintf(port, sizeof(port), "%u", STATUS_PORT);
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *res = NULL;
    if ((getaddrinfo(local_host, port, &hints, &res) != 0) || !res) {
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0), n = 0, got;
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
        while ((n < (int)sizeof(line) - 1) && ((got = recv(fd, line + n, sizeof(line) - 1 - n, 0)) > 0)) {
            n += got;
        }
    }
    freeaddrinfo(res);
    close(fd);
    line[n] = 0;
    int count = -1;
    char text[64] = "";
    if (sscanf(line, "%d %63[^\n]", &count, text) < 1) {
        return -1;
    }
    snprintf(result, len, "%s", text);
    return count;
}

// After a peer_notify case: waits up to 5 s for the server to record this connection's end
static void await_peer(conn_t *k, int before)
{
    strcpy(k->peer, "no report from the server");
    for (int i = 0; i < 50; i++) {
        char text[48];
        if (peer_status(text, sizeof(text)) > before) {
            snprintf(k->peer, sizeof(k->peer), "%s", text);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void conn_init(conn_t *k, const case_t *c)
{
    memset(k, 0, sizeof(*k));
    k->c = c;
    k->fd = -1;
    http_init(&k->http);
}

static void stubs_for(const case_t *c)
{
    stub_no_entropy = (c->stub == STUB_NO_ENTROPY);
    stub_sntp_time = (c->stub == STUB_SNTP_TIME);
}

static void run_case(const case_t *c)
{
    static conn_t k;
    conn_init(&k, c);
    stubs_for(c);
    engine_task_start();
    rpc_tls_stats_resp base, end;
    stats(&k, TLS_SLOT_GLOBAL, 0, &base);
    set_ca(&k);
    engine_alloc_reset();
    char text[48];
    int before = c->peer_notify ? peer_status(text, sizeof(text)) : 0;
    if (conn_open(&k, 0) && conn_handshake(&k) && conn_request(&k)) {
        conn_read_all(&k);
    }
    k.t_end = ms_now();
    conn_close(&k);
    if (c->peer_notify) {
        await_peer(&k, before);
    }
    stats(&k, TLS_SLOT_GLOBAL, 0, &end);
    uint32_t alloc_max = engine_alloc_max();
    uint32_t hwm = engine_task_stop();
    stubs_for(&(case_t){ 0 });
    report(&k, base.heap_free, &end, hwm, alloc_max);
}

// Two sessions at once (TLS_MAX_SESSIONS): both handshake, one after the other, then both downloads run
// interleaved, one READ round of each in turn
static void run_pair(const case_t *a, const case_t *b)
{
    static conn_t ka, kb;
    conn_init(&ka, a);
    conn_init(&kb, b);
    engine_task_start();
    rpc_tls_stats_resp base, end;
    stats(&ka, TLS_SLOT_GLOBAL, 0, &base);
    set_ca(&ka);
    engine_alloc_reset();
    bool a_up = conn_open(&ka, 0) && conn_handshake(&ka) && conn_request(&ka);
    bool b_up = conn_open(&kb, 1) && conn_handshake(&kb) && conn_request(&kb);
    bool a_done = !a_up, b_done = !b_up;
    while (!a_done || !b_done) {
        if (!a_done && conn_read_once(&ka, 5)) {
            a_done = true;
            ka.t_end = ms_now();
        }
        if (!b_done && conn_read_once(&kb, 5)) {
            b_done = true;
            kb.t_end = ms_now();
        }
    }
    conn_close(&ka);
    conn_close(&kb);
    stats(&ka, TLS_SLOT_GLOBAL, 0, &end);
    uint32_t alloc_max = engine_alloc_max();
    uint32_t hwm = engine_task_stop();
    report(&ka, base.heap_free, &end, hwm, alloc_max);
    report(&kb, base.heap_free, &end, hwm, alloc_max);
}

#define NIOS   TLS_TIME_NIOS
#define BUNDLE TLS_TRUST_BUNDLE
#define CUSTOM TLS_TRUST_CUSTOM
#define PIN    TLS_TRUST_PIN
#define TLS13  TLS_OPT_REQUIRE_TLS13
#define OK(...)  .expect = EXPECT_OK, .time_source_used = TLS_TIME_NIOS, __VA_ARGS__
#define FAILS(reason, f, alert) .expect = EXPECT_FAIL, .fail_reason = (reason), .flags = (f), .alert_in = (alert)
#define LOCAL_BLOB .body_len = BLOB_LEN, .body_sha = blob_sha256

static const case_t cases[] = {
    // Public hosts, Mozilla bundle: informational
    { "wikipedia", false, "en.wikipedia.org", 443, "en.wikipedia.org", "/wiki/Commodore_64", BUNDLE, 0, NULL, NIOS,
      OK(.version = 0x0304) },
    { "github", false, "github.com", 443, "github.com", "/robots.txt", BUNDLE, 0, NULL, NIOS, OK(.version = 0x0304) },
    { "example", false, "example.com", 443, "example.com", "/", BUNDLE, 0, NULL, NIOS, OK(.version = 0x0304) },
    { "gnu-rsa2048", false, "www.gnu.org", 443, "www.gnu.org", "/robots.txt", BUNDLE, 0, NULL, NIOS,
      OK(.version = 0x0304) },
    { "debian-rsa4096", false, "www.debian.org", 443, "www.debian.org", "/", BUNDLE, 0, NULL, NIOS,
      OK(.version = 0x0304) },
    { "c64wiki-tls12", false, "www.c64-wiki.com", 443, "www.c64-wiki.com", "/wiki/C64", BUNDLE, 0, NULL, NIOS,
      OK(.version = 0x0303) },
    // Answers HTTP/1.0 with 400, and keeps an HTTP/1.1 connection open after the reply despite Connection: close,
    // so the read ends on the idle timeout: accepted here only because the body is checked against Content-Length
    { "craigslist-tls12", false, "www.craigslist.org", 443, "www.craigslist.org", "/robots.txt", BUNDLE, 0, NULL, NIOS,
      .http11 = true, OK(.version = 0x0303, .ends = END_NOTIFY | END_IDLE | END_FIN) },
    { "badssl-no-ems", false, "tls-v1-2.badssl.com", 1012, "tls-v1-2.badssl.com", "/", BUNDLE, 0, NULL, NIOS,
      FAILS(TLS_FAIL_NO_EMS, 0, ANY) },
    { "c64wiki-require13", false, "www.c64-wiki.com", 443, "www.c64-wiki.com", "/", BUNDLE, TLS13, NULL, NIOS,
      FAILS(ANY, 0, 70) },
    // Gate, but needs the network: a Mozilla-trusted chain under the wrong name (Wikipedia serves *.wikipedia.org
    // to any SNI). BUNDLE has its own branch in tls_verify(), which no local server can reach.
    { "bundle-wronghost", true, "en.wikipedia.org", 443, "nomatch.invalid", "/", BUNDLE, 0, NULL, NIOS,
      FAILS(TLS_FAIL_CERT_VERIFY, 0x04, ANY), .flags_exact = true },
    // The local servers (tools/servers.sh), private CA uploaded as CUSTOM: the gate
    { "local-tls13", true, NULL, 8443, "tls.test", "/blob.bin", CUSTOM, 0, NULL, NIOS,
      OK(.version = 0x0304, LOCAL_BLOB) },
    { "local-tls12", true, NULL, 8444, "tls.test", "/blob.bin", CUSTOM, 0, NULL, NIOS,
      OK(.version = 0x0303, LOCAL_BLOB) },
    { "local-wronghost", true, NULL, 8445, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_CERT_VERIFY, 0x04, ANY) },
    { "local-selfsigned", true, NULL, 8446, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_CERT_VERIFY, 0x08, ANY) },
    { "local-selfsigned-bundle", true, NULL, 8446, "tls.test", "/", BUNDLE, 0, NULL, NIOS,
      FAILS(TLS_FAIL_CERT_VERIFY, 0x08, ANY) },
    { "local-expired", true, NULL, 8447, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_CERT_VERIFY, 0x01, ANY) },
    { "local-no-ems", true, NULL, 8448, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_NO_EMS, 0, ANY) },
    { "local-require13-vs-tls12", true, NULL, 8444, "tls.test", "/", CUSTOM, TLS13, NULL, NIOS, FAILS(ANY, 0, 70) },
    { "local-pin", true, NULL, 8443, "tls.test", "/blob.bin", PIN, 0, pin_good, NIOS, OK(.version = 0x0304, LOCAL_BLOB) },
    { "local-pin-wrong", true, NULL, 8443, "tls.test", "/", PIN, 0, pin_wrong, NIOS, FAILS(TLS_FAIL_PIN_MISMATCH, 0, ANY) },
    { "local-post-8k", true, NULL, 8449, "tls.test", "/echo", CUSTOM, 0, NULL, NIOS, .post_len = ECHO_LEN,
      OK(.version = 0x0304, .body_len = ECHO_REPLY_LEN, .body_sha = echo_reply_sha256) },
    { "local-close-established", true, NULL, 8449, "tls.test", "/keepalive", CUSTOM, 0, NULL, NIOS, .http11 = true,
      .close_early = true, .peer_notify = true, OK(.version = 0x0304, .ends = END_US, .body_len = 5) },
    { "local-no-time", true, NULL, 8443, "tls.test", "/", CUSTOM, 0, NULL, TLS_TIME_NONE,
      .expect = EXPECT_REFUSED, .open_reason = TLS_OPEN_REASON_NO_TIME },
    { "local-no-entropy", true, NULL, 8443, "tls.test", "/", CUSTOM, 0, NULL, NIOS, STUB_NO_ENTROPY,
      .expect = EXPECT_REFUSED, .open_reason = TLS_OPEN_REASON_NO_ENTROPY },
    { "local-sntp-clock", true, NULL, 8443, "tls.test", "/blob.bin", CUSTOM, 0, NULL, TLS_TIME_NONE, STUB_SNTP_TIME,
      .expect = EXPECT_OK, .version = 0x0304, LOCAL_BLOB, .time_source_used = TLS_TIME_SNTP },
};

static const case_t pair_a = { "pair-local-tls13", true, NULL, 8443, "tls.test", "/blob.bin", CUSTOM, 0, NULL, NIOS,
                               OK(.version = 0x0304, LOCAL_BLOB) };
static const case_t pair_b = { "pair-local-tls12", true, NULL, 8444, "tls.test", "/blob.bin", CUSTOM, 0, NULL, NIOS,
                               OK(.version = 0x0303, LOCAL_BLOB) };

static void client_task(void *arg)
{
    const char *path = arg;
    int n = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < n; i++) {
        gate_cases += cases[i].gate;
    }
    gate_cases += 2;
    printf("RUN start: %d cases + 1 interleaved pair, %d gate\n", n, gate_cases);
    for (int i = 0; i < n; i++) {
        run_case(&cases[i]);
    }
    run_pair(&pair_a, &pair_b);
    printf("SUMMARY net=%s gate_pass=%d gate_fail=%d info_pass=%d info_fail=%d\n", path, gate_pass, gate_fail,
           info_pass, info_fail);
    // A gate that failed only because a server was unreachable says nothing about the engine
    const char *verdict = ((gate_fail == 0) && (gate_pass == gate_cases)) ? "GREEN"
                          : ((gate_fail == gate_unreachable) && (gate_pass + gate_fail == gate_cases)) ? "INCONCLUSIVE"
                          : "RED";
    printf("VERDICT %s%s\n", verdict, strcmp(verdict, "INCONCLUSIVE") ? "" : " (network: a gate server was unreachable)");
    printf("STANDALONE_DONE\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
#ifdef ENGINE_MUTANT
    printf("*** MUTANT ENGINE: %s ***\n", ENGINE_MUTANT);
#endif
    printf("1541u-tls-standalone v" APP_VERSION ": ESP-IDF %s, mbedTLS %s, sources dated " __DATE__ " " __TIME__
           " (SOURCE_DATE_EPOCH)\n",
           esp_get_idf_version(), MBEDTLS_VERSION_STRING);
    // As the bridge's main.c: the bundle's per-certificate INFO lines off
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);
    const char *path = net_start();
    if (!path) {
        return;
    }
    // SNTP: in QEMU from tools/sntp_server.py on the host (10.0.2.2), so a run does not depend on a public NTP
    // server; on a board from pool.ntp.org. NVS "local" and "ntp" override both.
    char ntp[40];
    strcpy(ntp, strcmp(path, "wifi") ? local_host : "pool.ntp.org");
    nvs_handle_t h;
    if (nvs_open("standalone", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(local_host);
        nvs_get_str(h, "local", local_host, &n);
        n = sizeof(ntp);
        nvs_get_str(h, "ntp", ntp, &n);
        nvs_close(h);
    }
    printf("NTP %s\n", ntp);
    if (!net_sntp(ntp, 30000)) {
        printf("SNTP failed: no clock, so every BUNDLE/CUSTOM case will be refused NO_TIME\n");
    }
    time_t now = time(NULL);
    printf("CLOCK %lld %s", (long long)now, ctime(&now));
    printf("HEAP free %u largest %u (internal 8-bit)\n", (unsigned)heap_caps_get_free_size(TLS_HEAP_CAPS),
           (unsigned)heap_caps_get_largest_free_block(TLS_HEAP_CAPS));
    xTaskCreate(client_task, "Client", CLIENT_STACK, (void *)path, tskIDLE_PRIORITY + 1, NULL);
}
