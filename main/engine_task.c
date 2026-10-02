// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * engine_task.c
 *
 * The TLS task and the bridge's six TLS handlers. The handlers are the bridge's cmd_tls_open/step/close/stats/
 * close_all/set_ca and tls_serve (rpc_dispatch.c in the engine commit) with my_uart_transmit_packet() left out, so the
 * engine sees the same buffers: in particular STEP's reply overlays its request (resp->data at +20, req->data at +12).
 *
 * Origin: those handlers are transcribed from software/wifi/raw_u64/main/rpc_dispatch.c of the 1541 Ultimate
 * firmware (GideonZ/1541ultimate, GPL-3.0-or-later); tools/verify_transcriptions.py checks them against the engine commit.
 */

#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "engine_task.h"
#include "wifi_modem.h"

typedef struct {
    command_buf_t *buf;
    uint32_t *call_us;
    TaskHandle_t caller;
} msg_t;

static QueueHandle_t queue;
static SemaphoreHandle_t stopped;
static volatile TaskHandle_t tls_task_handle;
static volatile uint32_t alloc_max;
static uint32_t final_hwm;

// CONFIG_HEAP_USE_HOOKS: called for every allocation; only the TLS task's are counted
void esp_heap_trace_alloc_hook(void *ptr, size_t size, uint32_t caps)
{
    if (ptr && (xTaskGetCurrentTaskHandle() == tls_task_handle) && (size > alloc_max)) {
        alloc_max = size;
    }
}

void esp_heap_trace_free_hook(void *ptr)
{
}

void engine_alloc_reset(void)
{
    alloc_max = 0;
}

uint32_t engine_alloc_max(void)
{
    return alloc_max;
}

// rpc_dispatch.c:375-400
static void cmd_tls_open(command_buf_t *buf)
{
    rpc_tls_open_req *req = (rpc_tls_open_req *)buf->data;
    rpc_tls_open_resp *resp = (rpc_tls_open_resp *)buf->data;
    uint8_t slot = TLS_SLOT_GLOBAL;
    uint8_t reason = TLS_OPEN_REASON_NONE;
    int err = ESP_ERR_INVALID_ARG;
    if ((buf->size >= (int)sizeof(rpc_tls_open_req)) && (buf->size == (int)sizeof(rpc_tls_open_req) + req->sni_len)) {
        tls_open_params_t p = {
            .trust = req->trust, .opts = req->opts, .time_source = req->time_source, .ca_tag = req->ca_tag,
            .unix_time = req->unix_time, .pin = req->pin, .sni = req->sni, .sni_len = req->sni_len,
        };
        err = tls_open(req->slot, req->tag, &p, &reason);
        if (err == ESP_OK) {
            slot = req->slot;
        }
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_open_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->slot = slot;
    resp->reason = reason;
    buf->size = sizeof(rpc_tls_open_resp);
}

// rpc_dispatch.c:402-435: the response overlays the request
static void cmd_tls_step(command_buf_t *buf)
{
    rpc_tls_step_req *req = (rpc_tls_step_req *)buf->data;
    rpc_tls_step_resp *resp = (rpc_tls_step_resp *)buf->data;
    uint16_t ct_out_len = 0;
    uint16_t pt_out_len = 0;
    tls_step_result_t result = { .state = TLS_STATE_CLOSED, .alert_in = TLS_ALERT_NONE, .alert_out = TLS_ALERT_NONE };
    int err = ESP_ERR_INVALID_ARG;
    if (buf->size >= (int)sizeof(rpc_tls_step_req)) {
        int ct_in_len = req->ct_in_len;
        int pt_in_len = req->pt_in_len;
        if (buf->size == (int)sizeof(rpc_tls_step_req) + ct_in_len + pt_in_len) {
            err = tls_step(req->slot, req->tag, req->flags, req->data, ct_in_len, req->data + ct_in_len, pt_in_len,
                           resp->data, &ct_out_len, &pt_out_len, &result);
        } else {
            tls_session_stats_t stats;
            tls_get_stats(req->slot, req->tag, &stats);
            result.state = stats.state;
        }
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_step_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->state = result.state;
    resp->alert_in = result.alert_in;
    resp->ct_out_len = ct_out_len;
    resp->pt_out_len = pt_out_len;
    resp->alert_out = result.alert_out;
    resp->fail_reason = result.fail_reason;
    resp->verify_flags = result.verify_flags;
    buf->size = sizeof(rpc_tls_step_resp) + ct_out_len + pt_out_len;
}

// rpc_dispatch.c:437-453
static void cmd_tls_close(command_buf_t *buf)
{
    rpc_tls_session_req *req = (rpc_tls_session_req *)buf->data;
    rpc_tls_close_resp *resp = (rpc_tls_close_resp *)buf->data;
    uint16_t ct_out_len = 0;
    int err = ESP_ERR_INVALID_ARG;
    if (buf->size >= (int)sizeof(rpc_tls_session_req)) {
        err = tls_close(req->slot, req->tag, resp->data, &ct_out_len);
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_close_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->ct_out_len = ct_out_len;
    buf->size = sizeof(rpc_tls_close_resp) + ct_out_len;
}

// rpc_dispatch.c:455-492 (block_max_ms: there is no dispatcher here, so 0)
static void cmd_tls_stats(command_buf_t *buf)
{
    rpc_tls_session_req *req = (rpc_tls_session_req *)buf->data;
    rpc_tls_stats_resp *resp = (rpc_tls_stats_resp *)buf->data;
    tls_session_stats_t stats;
    int err = ESP_ERR_INVALID_ARG;
    if (buf->size >= (int)sizeof(rpc_tls_session_req)) {
        err = tls_get_stats(req->slot, req->tag, &stats);
    } else {
        tls_get_stats(TLS_SLOT_GLOBAL, 0, &stats);
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_stats_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->heap_free = heap_caps_get_free_size(TLS_HEAP_CAPS);
    resp->heap_min = heap_caps_get_minimum_free_size(TLS_HEAP_CAPS);
    resp->largest_block = heap_caps_get_largest_free_block(TLS_HEAP_CAPS);
    resp->ct_in_bytes = stats.ct_in_bytes;
    resp->ct_out_bytes = stats.ct_out_bytes;
    resp->pt_in_bytes = stats.pt_in_bytes;
    resp->pt_out_bytes = stats.pt_out_bytes;
    resp->handshake_us = stats.handshake_us;
    resp->tls_version = stats.tls_version;
    resp->ciphersuite = stats.ciphersuite;
    resp->group = stats.group;
    resp->state = stats.state;
    resp->sessions_open = tls_sessions_open();
    resp->stack_free = uxTaskGetStackHighWaterMark(NULL); // bytes in ESP-IDF
    resp->esp_time = stats.esp_time;
    resp->time_source = stats.time_source;
    resp->entropy_source = entropy_source();
    resp->ems = stats.ems;
    resp->block_max_ms = 0;
    memcpy(resp->spki_sha256, stats.spki_sha256, sizeof(resp->spki_sha256));
    buf->size = sizeof(rpc_tls_stats_resp);
}

// rpc_dispatch.c:494-501
static void cmd_tls_close_all(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    tls_close_all();
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
}

// rpc_dispatch.c:503-521
static void cmd_tls_set_ca(command_buf_t *buf)
{
    rpc_tls_set_ca_req *req = (rpc_tls_set_ca_req *)buf->data;
    rpc_tls_set_ca_resp *resp = (rpc_tls_set_ca_resp *)buf->data;
    uint8_t reject = TLS_CA_REJECT_NONE;
    int err = ESP_ERR_INVALID_ARG;
    if ((buf->size >= (int)sizeof(rpc_tls_set_ca_req)) && (buf->size == (int)sizeof(rpc_tls_set_ca_req) + req->der_len)) {
        err = tls_set_ca(req->der, req->der_len, req->ca_tag, &reject);
    } else {
        tls_set_ca(NULL, 0, 0, &reject);
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_set_ca_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->reject = reject;
    buf->size = sizeof(rpc_tls_set_ca_resp);
}

// rpc_dispatch.c:524-550 (tls_serve)
static void serve(command_buf_t *buf)
{
    switch (((rpc_header_t *)buf->data)->command) {
    case CMD_TLS_OPEN:
        cmd_tls_open(buf);
        break;
    case CMD_TLS_STEP:
        cmd_tls_step(buf);
        break;
    case CMD_TLS_CLOSE:
        cmd_tls_close(buf);
        break;
    case CMD_TLS_STATS:
        cmd_tls_stats(buf);
        break;
    case CMD_TLS_CLOSE_ALL:
        cmd_tls_close_all(buf);
        break;
    case CMD_TLS_SET_CA:
        cmd_tls_set_ca(buf);
        break;
    default:
        buf->size = 0;
        break;
    }
}

static void tls_task(void *arg)
{
    msg_t m;
    while (xQueueReceive(queue, &m, portMAX_DELAY) == pdTRUE) {
        if (!m.buf) {
            break;
        }
        int64_t start = esp_timer_get_time();
        serve(m.buf);
        *m.call_us = (uint32_t)(esp_timer_get_time() - start);
        xTaskNotifyGive(m.caller);
    }
    final_hwm = uxTaskGetStackHighWaterMark(NULL);
    tls_task_handle = NULL;
    xSemaphoreGive(stopped);
    vTaskDelete(NULL);
}

void engine_task_start(void)
{
    if (!queue) {
        queue = xQueueCreate(1, sizeof(msg_t));
        stopped = xSemaphoreCreateBinary();
    }
    TaskHandle_t h;
    xTaskCreatePinnedToCore(tls_task, "TLS", TLS_TASK_STACK, NULL, tskIDLE_PRIORITY + 1, &h, 1);
    tls_task_handle = h;
}

uint32_t engine_task_stop(void)
{
    static command_buf_t buf;
    uint32_t us;
    memset(buf.data, 0, sizeof(rpc_header_t));
    ((rpc_header_t *)buf.data)->command = CMD_TLS_CLOSE_ALL;
    buf.size = sizeof(rpc_header_t);
    engine_call(&buf, &us);
    msg_t m = { NULL, NULL, NULL };
    xQueueSend(queue, &m, portMAX_DELAY);
    xSemaphoreTake(stopped, portMAX_DELAY);
    return final_hwm;
}

void engine_call(command_buf_t *buf, uint32_t *call_us)
{
    msg_t m = { buf, call_us, xTaskGetCurrentTaskHandle() };
    xQueueSend(queue, &m, portMAX_DELAY);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}
