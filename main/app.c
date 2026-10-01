/*
 * app.c
 *
 * 1541u-tls-standalone: the 1541 Ultimate's ESP32 TLS engine (tls_engine.c, compiled unmodified) with nothing
 * of the Ultimate around it. FreeRTOS, lwIP and mbedTLS from ESP-IDF; this file plays the Ultimate's part:
 * it owns the TCP socket and pumps ciphertext through tls_step() the way network_target.cc does over RPC
 * (handshake, WRITE with NO_READ, eager READ, resend on ESP_ERR_NO_MEM, CLOSE with close_notify).
 *
 * Every case prints one RESULT line and one CASE line (PASS or FAIL against its expectation).
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
#include "net.h"
#include "pins.h"

#define APP_VERSION      "1"
#define CLIENT_STACK     16384
#define HANDSHAKE_MS     30000
#define IDLE_MS          20000 // no byte from the server for this long ends a read
#define TOTAL_MS         240000
#define ANY              -1

extern const uint8_t ca_der_start[] asm("_binary_ca_der_start");
extern const uint8_t ca_der_end[] asm("_binary_ca_der_end");

static const uint8_t pin_wrong[32] = { 0xde, 0xad, 0xbe, 0xef };
static char local_host[40] = "10.0.2.2"; // QEMU user networking: the host; NVS "local" overrides it

typedef struct {
    const char *name;
    const char *host;         // NULL: the local OpenSSL server
    uint16_t port;
    const char *sni;
    const char *path;
    uint8_t trust;
    uint8_t opts;
    const uint8_t *pin;
    uint8_t time_source;
    // Expectations
    int expect_ok;            // 1: ESTABLISHED and an HTTP reply; 0: a failure as below
    int expect_open_reason;   // ANY, or the TLS_OPEN_REASON_* an OPEN refusal must carry
    int expect_fail_reason;   // ANY, or the tls_fail_reason_t
    uint32_t expect_flags;    // verify_flags bits that must be set
    uint16_t expect_version;  // 0: any
} case_t;

typedef struct {
    const case_t *c;
    int fd;
    uint8_t slot;
    uint16_t tag;
    uint8_t held[TLS_STEP_IN_MAX];
    int held_len;
    bool more;
    bool rx_fin;
    int rx_err;
    int quiet;
    // What happened
    const char *outcome;
    int open_ret;
    uint8_t open_reason;
    int esp_err;
    uint8_t state, fail_reason, alert_in, alert_out;
    uint32_t verify_flags;
    tls_session_stats_t hs;   // STATS right after ESTABLISHED, as the Ultimate reads it
    uint32_t hs_stack_free;
    int64_t t_open, t_established, t_req_sent, t_end;
    uint32_t pt_bytes, ct_in, ct_out;
    char http[64];
    uint32_t steps, step_us_max, hs_engine_us, xfer_engine_us;
    req_t req;
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

// One STEP; returns 0 (input taken), 1 (ESP_ERR_NO_MEM: send the same input again) or -1 (refused: the
// engine did not answer with a state, which the Ultimate calls a module error)
static int step(conn_t *k, uint8_t flags, const uint8_t *pt, int pt_len, bool handshake)
{
    req_t *r = &k->req;
    memset(r, 0, offsetof(req_t, out));
    r->cmd = REQ_STEP;
    r->slot = k->slot;
    r->tag = k->tag;
    r->flags = flags;
    r->ct_in = k->held;
    r->ct_in_len = k->held_len;
    r->pt_in = pt;
    r->pt_in_len = pt_len;
    int ret = engine_call(r);
    k->steps++;
    if (r->call_us > k->step_us_max) {
        k->step_us_max = r->call_us;
    }
    if (handshake) {
        k->hs_engine_us += r->call_us;
    } else {
        k->xfer_engine_us += r->call_us;
    }
    k->state = r->result.state;
    k->esp_err = ret;
    bool again = (ret == ESP_ERR_NO_MEM) && (k->state <= TLS_STATE_CLOSED);
    if ((ret != 0) && !again && (k->state != TLS_STATE_FAILED)) {
        return -1;
    }
    if (k->state == TLS_STATE_FAILED) {
        k->fail_reason = r->result.fail_reason;
        k->alert_in = r->result.alert_in;
        k->alert_out = r->result.alert_out;
        k->verify_flags = r->result.verify_flags;
        r->pt_out_len = 0; // withheld, as wifi_tls.cc does
    }
    if (!again) {
        k->ct_in += k->held_len;
        k->held_len = 0;
    }
    k->ct_out += r->ct_out_len;
    if (r->ct_out_len && !send_all(k, r->out, r->ct_out_len) && (k->state != TLS_STATE_FAILED)) {
        k->outcome = "send-error";
        return -1;
    }
    return again ? 1 : 0;
}

static void take_plaintext(conn_t *k)
{
    req_t *r = &k->req;
    if (!r->pt_out_len) {
        return;
    }
    const uint8_t *p = r->out + r->ct_out_len;
    if (k->pt_bytes == 0) {
        int n = 0;
        while ((n < r->pt_out_len) && (n < (int)sizeof(k->http) - 1) && (p[n] != '\r') && (p[n] != '\n')) {
            k->http[n] = (p[n] >= 32 && p[n] < 127 && p[n] != '"') ? p[n] : '?';
            n++;
        }
        k->http[n] = 0;
    }
    k->pt_bytes += r->pt_out_len;
}

static bool conn_connect(conn_t *k)
{
    const case_t *c = k->c;
    char port[8];
    snprintf(port, sizeof(port), "%u", c->port);
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *res = NULL;
    if ((getaddrinfo(c->host ? c->host : local_host, port, &hints, &res) != 0) || !res) {
        k->outcome = "dns-failed";
        return false;
    }
    k->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval tv = { 10, 0 };
    setsockopt(k->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int ok = connect(k->fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok) {
        k->outcome = "connect-failed";
        close(k->fd);
        k->fd = -1;
        return false;
    }
    int on = 1;
    fcntl(k->fd, F_SETFL, fcntl(k->fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(k->fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    return true;
}

static void stats(conn_t *k, uint8_t slot, uint16_t tag, req_t *out)
{
    memset(out, 0, sizeof(*out));
    out->cmd = REQ_STATS;
    out->slot = slot;
    out->tag = tag;
    engine_call(out);
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
    tls_open_params_t p = {
        .trust = c->trust, .opts = c->opts, .time_source = c->time_source, .ca_tag = 0x5a5a,
        .unix_time = (c->time_source == TLS_TIME_NIOS) ? (uint32_t)time(NULL) : 0,
        .pin = c->pin, .sni = c->sni, .sni_len = c->sni ? strlen(c->sni) : 0,
    };
    req_t *r = &k->req;
    memset(r, 0, offsetof(req_t, out));
    r->cmd = REQ_OPEN;
    r->slot = slot;
    r->tag = k->tag;
    r->open = &p;
    k->t_open = ms_now();
    k->open_ret = engine_call(r);
    k->open_reason = r->reason;
    if (k->open_ret != 0) {
        k->outcome = "open-refused";
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
            k->outcome = "handshake-timeout";
            return false;
        }
        if ((k->held_len == 0) && !k->more) {
            if (!wait_readable(k->fd, 100)) {
                continue;
            }
            int n = recv(k->fd, k->held, sizeof(k->held), MSG_DONTWAIT);
            if (n == 0) {
                k->outcome = "peer-closed";
                return false;
            }
            if (n < 0) {
                if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
                    continue;
                }
                k->outcome = "peer-reset";
                return false;
            }
            k->held_len = n;
        }
        int s = step(k, 0, NULL, 0, true);
        if (s < 0) {
            if (!k->outcome) {
                k->outcome = "module-error";
            }
            return false;
        }
        if (k->state == TLS_STATE_FAILED) {
            k->outcome = "tls-failed";
            return false;
        }
        if (k->state == TLS_STATE_CLOSED) {
            k->outcome = "peer-closed";
            return false;
        }
        if (k->state == TLS_STATE_ESTABLISHED) {
            take_plaintext(k);
            break;
        }
        k->more = (k->req.ct_out_len != 0) || (s == 1);
    }
    k->t_established = ms_now();
    req_t st;
    stats(k, k->slot, k->tag, &st);
    k->hs = st.stats;
    k->hs_stack_free = st.stack_free;
    if ((st.ret != 0) || (k->hs.group == 0)) {
        k->outcome = "stats-failed";
        return false;
    }
    return true;
}

// tls_write(): NO_READ steps until all of it is taken and nothing is left to send
static bool conn_write(conn_t *k, const uint8_t *data, int len)
{
    int pending = len;
    for (int steps = 0; steps < 4; steps++) {
        int s = step(k, TLS_STEP_NO_READ, data, pending, false);
        if (s < 0 || (k->state != TLS_STATE_ESTABLISHED)) {
            if (!k->outcome) {
                k->outcome = (k->state == TLS_STATE_FAILED) ? "tls-failed" : "module-error";
            }
            return false;
        }
        if (s == 0) {
            pending = 0;
        }
        if ((pending == 0) && (k->req.ct_out_len == 0)) {
            return true;
        }
    }
    k->outcome = "write-budget";
    return false;
}

// tls_read(), looped until the stream ends: eager top-up, a step, the ciphertext out, the plaintext taken
static void conn_read_all(conn_t *k)
{
    int64_t deadline = ms_now() + TOTAL_MS, idle_until = ms_now() + IDLE_MS;
    k->more = true;
    while (true) {
        if (ms_now() > deadline) {
            k->outcome = "read-timeout";
            return;
        }
        if (!k->rx_err && !k->rx_fin && (k->held_len < TLS_STEP_IN_MAX)) {
            int n = recv(k->fd, k->held + k->held_len, TLS_STEP_IN_MAX - k->held_len, MSG_DONTWAIT);
            if (n > 0) {
                k->held_len += n;
                idle_until = ms_now() + IDLE_MS;
            } else if (n == 0) {
                k->rx_fin = true;
            } else if ((errno != EWOULDBLOCK) && (errno != EAGAIN)) {
                k->rx_err = errno;
            }
        }
        bool draining = k->rx_err || k->rx_fin;
        if ((k->held_len == 0) && !k->more && !draining) {
            if (ms_now() > idle_until) {
                k->outcome = "idle-timeout";
                return;
            }
            wait_readable(k->fd, 100);
            continue;
        }
        int fed = k->held_len;
        int s = step(k, 0, NULL, 0, false);
        if (s < 0) {
            if (!k->outcome) {
                k->outcome = "module-error";
            }
            return;
        }
        take_plaintext(k);
        k->more = (k->req.ct_out_len != 0) || (k->req.pt_out_len != 0) || (s == 1);
        if (k->state == TLS_STATE_FAILED) {
            k->outcome = "tls-failed";
            return;
        }
        if (k->state == TLS_STATE_CLOSED) {
            k->outcome = "close-notify";
            return;
        }
        if (k->req.pt_out_len) {
            k->quiet = 0;
        } else if (draining && (fed == 0)) {
            if (++k->quiet >= 2) {
                k->outcome = k->rx_err ? "rx-error" : "tcp-fin-no-notify";
                return;
            }
        } else {
            k->quiet = 0;
        }
    }
}

static void conn_close(conn_t *k)
{
    if (k->state == TLS_STATE_ESTABLISHED || k->state == TLS_STATE_HANDSHAKING) {
        req_t *r = &k->req;
        memset(r, 0, offsetof(req_t, out));
        r->cmd = REQ_CLOSE;
        r->slot = k->slot;
        r->tag = k->tag;
        if ((engine_call(r) == 0) && r->ct_out_len && (k->fd >= 0)) {
            send_all(k, r->out, r->ct_out_len);
        }
    }
    if (k->fd >= 0) {
        close(k->fd);
        k->fd = -1;
    }
}

static bool conn_request(conn_t *k)
{
    char req[256];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: 1541u-tls-standalone/" APP_VERSION
                     "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                     k->c->path, k->c->sni ? k->c->sni : "localhost");
    if (!conn_write(k, (const uint8_t *)req, n)) {
        return false;
    }
    k->t_req_sent = ms_now();
    return true;
}

static bool judge(const conn_t *k, char *why, int len)
{
    const case_t *c = k->c;
    bool ok = (k->state == TLS_STATE_CLOSED || k->state == TLS_STATE_ESTABLISHED) && k->t_established &&
              (strncmp(k->http, "HTTP/1.", 7) == 0);
    if (c->expect_ok) {
        if (!ok) {
            snprintf(why, len, "expected a reply, got %s", k->outcome ? k->outcome : "?");
            return false;
        }
        if (c->expect_version && (k->hs.tls_version != c->expect_version)) {
            snprintf(why, len, "expected version 0x%04x", c->expect_version);
            return false;
        }
        snprintf(why, len, "%s", k->http);
        return true;
    }
    if (c->expect_open_reason != ANY) {
        bool hit = (k->open_ret != 0) && (k->open_reason == c->expect_open_reason);
        snprintf(why, len, "open %d reason %u", k->open_ret, k->open_reason);
        return hit;
    }
    if (k->state != TLS_STATE_FAILED || k->t_established) {
        snprintf(why, len, "expected a handshake failure, got %s", k->outcome ? k->outcome : "?");
        return false;
    }
    if ((c->expect_fail_reason != ANY) && (k->fail_reason != c->expect_fail_reason)) {
        snprintf(why, len, "fail_reason %u, expected %d", k->fail_reason, c->expect_fail_reason);
        return false;
    }
    if ((k->verify_flags & c->expect_flags) != c->expect_flags) {
        snprintf(why, len, "verify_flags 0x%08x lack 0x%08x", (unsigned)k->verify_flags, (unsigned)c->expect_flags);
        return false;
    }
    snprintf(why, len, "fail_reason %u verify 0x%08x alert_in %u", k->fail_reason, (unsigned)k->verify_flags,
             k->alert_in);
    return true;
}

static int passed, failed;

static void report(conn_t *k, uint32_t heap_before, const req_t *end, uint32_t stack_free, uint32_t alloc_max)
{
    const case_t *c = k->c;
    char why[96];
    bool pass = judge(k, why, sizeof(why));
    pass ? passed++ : failed++;
    int64_t xfer_ms = (k->t_req_sent && k->t_end > k->t_req_sent) ? k->t_end - k->t_req_sent : 0;
    printf("RESULT case=%s host=%s port=%u sni=%s trust=%u opts=%u outcome=%s open_ret=%d open_reason=%u "
           "state=%u esp_err=%d fail_reason=%u verify=0x%08x alert_in=%u alert_out=%u "
           "version=0x%04x suite=0x%04x group=0x%04x ems=%u http=\"%s\" pt_bytes=%u ct_in=%u ct_out=%u "
           "hs_wall_ms=%lld hs_mbedtls_ms=%u.%03u hs_engine_ms=%u.%03u xfer_ms=%lld xfer_engine_ms=%u.%03u "
           "steps=%u step_max_ms=%u.%03u heap_before=%u heap_min=%u heap_peak_use=%d largest_free=%u "
           "largest_alloc=%u stack_free_hs=%u stack_free=%u stack_size=%u\n",
           c->name, c->host ? c->host : local_host, c->port, c->sni ? c->sni : "-", c->trust, c->opts,
           k->outcome ? k->outcome : "?", k->open_ret, k->open_reason, k->state, k->esp_err, k->fail_reason,
           (unsigned)k->verify_flags, k->alert_in, k->alert_out, k->hs.tls_version, k->hs.ciphersuite, k->hs.group,
           k->hs.ems, k->http, (unsigned)k->pt_bytes, (unsigned)k->ct_in, (unsigned)k->ct_out,
           (long long)(k->t_established ? k->t_established - k->t_open : 0), (unsigned)k->hs.handshake_us / 1000,
           (unsigned)k->hs.handshake_us % 1000, (unsigned)k->hs_engine_us / 1000, (unsigned)k->hs_engine_us % 1000,
           (long long)xfer_ms, (unsigned)k->xfer_engine_us / 1000, (unsigned)k->xfer_engine_us % 1000, (unsigned)k->steps,
           (unsigned)k->step_us_max / 1000, (unsigned)k->step_us_max % 1000, (unsigned)heap_before,
           (unsigned)end->heap_min, (int)(heap_before - end->heap_min), (unsigned)end->largest_block,
           (unsigned)alloc_max, (unsigned)k->hs_stack_free, (unsigned)stack_free, TLS_TASK_STACK);
    printf("CASE %s %s (%s)\n", c->name, pass ? "PASS" : "FAIL", why);
}

static void set_ca(void)
{
    req_t r = { .cmd = REQ_SET_CA, .der = ca_der_start, .der_len = ca_der_end - ca_der_start, .ca_tag = 0x5a5a };
    if (engine_call(&r) != 0) {
        printf("SET_CA failed: %d reject %u\n", r.ret, r.reject);
    }
}

static void run_case(const case_t *c)
{
    static conn_t k;
    memset(&k, 0, sizeof(k));
    k.c = c;
    engine_task_start();
    req_t base;
    stats(&k, TLS_SLOT_GLOBAL, 0, &base);
    set_ca();
    engine_alloc_reset();
    if (conn_open(&k, 0) && conn_handshake(&k) && conn_request(&k)) {
        conn_read_all(&k);
    }
    k.t_end = ms_now();
    conn_close(&k);
    req_t end;
    stats(&k, TLS_SLOT_GLOBAL, 0, &end);
    uint32_t alloc_max = engine_alloc_max();
    uint32_t hwm = engine_task_stop();
    report(&k, base.heap_free, &end, hwm, alloc_max);
}

// Two sessions at once (TLS_MAX_SESSIONS): a established and idle while b runs to the end, then a
static void run_pair(const case_t *a, const case_t *b)
{
    static conn_t ka, kb;
    memset(&ka, 0, sizeof(ka));
    memset(&kb, 0, sizeof(kb));
    ka.c = a;
    kb.c = b;
    engine_task_start();
    req_t base;
    stats(&ka, TLS_SLOT_GLOBAL, 0, &base);
    set_ca();
    engine_alloc_reset();
    bool a_up = conn_open(&ka, 0) && conn_handshake(&ka);
    if (conn_open(&kb, 1) && conn_handshake(&kb) && conn_request(&kb)) {
        conn_read_all(&kb);
    }
    kb.t_end = ms_now();
    conn_close(&kb);
    if (a_up && conn_request(&ka)) {
        conn_read_all(&ka);
    }
    ka.t_end = ms_now();
    conn_close(&ka);
    req_t end;
    stats(&ka, TLS_SLOT_GLOBAL, 0, &end);
    uint32_t alloc_max = engine_alloc_max();
    uint32_t hwm = engine_task_stop();
    report(&kb, base.heap_free, &end, hwm, alloc_max);
    report(&ka, base.heap_free, &end, hwm, alloc_max);
}

#define NIOS TLS_TIME_NIOS
#define OK(v) 1, ANY, ANY, 0, v
#define FAILS(reason, flags) 0, ANY, reason, flags, 0
#define REFUSED(reason) 0, reason, ANY, 0, 0
#define BUNDLE TLS_TRUST_BUNDLE
#define CUSTOM TLS_TRUST_CUSTOM
#define PIN TLS_TRUST_PIN
#define TLS13 TLS_OPT_REQUIRE_TLS13

static const case_t cases[] = {
    // Public hosts, Mozilla bundle
    { "wikipedia",   "en.wikipedia.org", 443, "en.wikipedia.org", "/wiki/Commodore_64", BUNDLE, 0, NULL, NIOS, OK(0x0304) },
    { "github",      "github.com", 443, "github.com", "/robots.txt", BUNDLE, 0, NULL, NIOS, OK(0x0304) },
    { "example",     "example.com", 443, "example.com", "/", BUNDLE, 0, NULL, NIOS, OK(0x0304) },
    { "gnu-rsa2048", "www.gnu.org", 443, "www.gnu.org", "/robots.txt", BUNDLE, 0, NULL, NIOS, OK(0x0304) },
    { "debian-rsa4096", "www.debian.org", 443, "www.debian.org", "/robots.txt", BUNDLE, 0, NULL, NIOS, OK(0x0304) },
    { "c64wiki-tls12", "www.c64-wiki.com", 443, "www.c64-wiki.com", "/wiki/C64", BUNDLE, 0, NULL, NIOS, OK(0x0303) },
    { "craigslist-tls12", "www.craigslist.org", 443, "www.craigslist.org", "/robots.txt", BUNDLE, 0, NULL, NIOS, OK(0x0303) },
    { "badssl-no-ems", "tls-v1-2.badssl.com", 1012, "tls-v1-2.badssl.com", "/", BUNDLE, 0, NULL, NIOS,
      FAILS(TLS_FAIL_NO_EMS, 0) },
    { "c64wiki-require13", "www.c64-wiki.com", 443, "www.c64-wiki.com", "/", BUNDLE, TLS13, NULL, NIOS, FAILS(ANY, 0) },
    // The local OpenSSL servers (tools/run_qemu.sh), private CA uploaded as CUSTOM
    { "local-tls13",      NULL, 8443, "tls.test", "/blob.bin", CUSTOM, 0, NULL, NIOS, OK(0x0304) },
    { "local-tls12",      NULL, 8444, "tls.test", "/blob.bin", CUSTOM, 0, NULL, NIOS, OK(0x0303) },
    { "local-wronghost",  NULL, 8445, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_CERT_VERIFY, 0x04) },
    { "local-selfsigned", NULL, 8446, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_CERT_VERIFY, 0x08) },
    { "local-selfsigned-bundle", NULL, 8446, "tls.test", "/", BUNDLE, 0, NULL, NIOS, FAILS(TLS_FAIL_CERT_VERIFY, 0x08) },
    { "local-expired",    NULL, 8447, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_CERT_VERIFY, 0x01) },
    { "local-no-ems",     NULL, 8448, "tls.test", "/", CUSTOM, 0, NULL, NIOS, FAILS(TLS_FAIL_NO_EMS, 0) },
    { "local-require13-vs-tls12", NULL, 8444, "tls.test", "/", CUSTOM, TLS13, NULL, NIOS, FAILS(ANY, 0) },
    { "local-pin",        NULL, 8443, "tls.test", "/blob.bin", PIN, 0, pin_good, NIOS, OK(0x0304) },
    { "local-pin-wrong",  NULL, 8443, "tls.test", "/", PIN, 0, pin_wrong, NIOS, FAILS(TLS_FAIL_PIN_MISMATCH, 0) },
    { "local-no-time",    NULL, 8443, "tls.test", "/", CUSTOM, 0, NULL, TLS_TIME_NONE, REFUSED(TLS_OPEN_REASON_NO_TIME) },
};

static const case_t pair_local = { "pair-local-tls13", NULL, 8443, "tls.test", "/blob.bin", CUSTOM, 0, NULL, NIOS, OK(0x0304) };
static const case_t pair_wiki = { "pair-wikipedia", "en.wikipedia.org", 443, "en.wikipedia.org", "/wiki/Commodore_64",
                                  BUNDLE, 0, NULL, NIOS, OK(0x0304) };

static void client_task(void *arg)
{
    const char *path = arg;
    printf("RUN start: %d cases + 1 pair\n", (int)(sizeof(cases) / sizeof(cases[0])));
    for (int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        run_case(&cases[i]);
    }
    run_pair(&pair_local, &pair_wiki);
    printf("SUMMARY net=%s pass=%d fail=%d\n", path, passed, failed);
    printf("STANDALONE_DONE\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("1541u-tls-standalone v" APP_VERSION ": ESP-IDF %s, mbedTLS %s, engine sources pinned at f2e46946, "
           "built " __DATE__ " " __TIME__ "\n", esp_get_idf_version(), MBEDTLS_VERSION_STRING);
    // As the bridge's main.c: the bundle's per-certificate INFO lines off
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);
    const char *path = net_start();
    if (!path) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open("standalone", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(local_host);
        nvs_get_str(h, "local", local_host, &n);
        nvs_close(h);
    }
    if (!net_sntp(30000)) {
        printf("SNTP failed: no clock, so every BUNDLE/CUSTOM case will be refused NO_TIME\n");
    }
    time_t now = time(NULL);
    printf("CLOCK %lld %s", (long long)now, ctime(&now));
    printf("HEAP free %u largest %u (internal 8-bit)\n", (unsigned)heap_caps_get_free_size(TLS_HEAP_CAPS),
           (unsigned)heap_caps_get_largest_free_block(TLS_HEAP_CAPS));
    xTaskCreate(client_task, "Client", CLIENT_STACK, (void *)path, tskIDLE_PRIORITY + 1, NULL);
}
