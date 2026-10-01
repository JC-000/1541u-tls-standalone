/*
 * engine_task.c
 *
 * The TLS task and its request queue. The heap figures of a STATS request are taken as the bridge's
 * cmd_tls_stats() takes them (rpc_dispatch.c at f2e46946), on the TLS task.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "engine_task.h"

typedef struct {
    req_t *req;
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

static void run(req_t *r)
{
    switch (r->cmd) {
    case REQ_OPEN:
        r->ret = tls_open(r->slot, r->tag, r->open, &r->reason);
        break;
    case REQ_STEP:
        r->ret = tls_step(r->slot, r->tag, r->flags, r->ct_in, r->ct_in_len, r->pt_in, r->pt_in_len, r->out,
                          &r->ct_out_len, &r->pt_out_len, &r->result);
        break;
    case REQ_CLOSE:
        r->ret = tls_close(r->slot, r->tag, r->out, &r->ct_out_len);
        break;
    case REQ_SET_CA:
        r->ret = tls_set_ca(r->der, r->der_len, r->ca_tag, &r->reject);
        break;
    case REQ_CLOSE_ALL:
        tls_close_all();
        r->ret = 0;
        break;
    case REQ_STATS:
        r->ret = tls_get_stats(r->slot, r->tag, &r->stats);
        r->heap_free = heap_caps_get_free_size(TLS_HEAP_CAPS);
        r->heap_min = heap_caps_get_minimum_free_size(TLS_HEAP_CAPS);
        r->largest_block = heap_caps_get_largest_free_block(TLS_HEAP_CAPS);
        r->stack_free = uxTaskGetStackHighWaterMark(NULL); // bytes in ESP-IDF
        break;
    }
}

static void tls_task(void *arg)
{
    msg_t m;
    while (xQueueReceive(queue, &m, portMAX_DELAY) == pdTRUE) {
        if (!m.req) {
            break;
        }
        int64_t start = esp_timer_get_time();
        run(m.req);
        m.req->call_us = (uint32_t)(esp_timer_get_time() - start);
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
    req_t r = { .cmd = REQ_CLOSE_ALL };
    engine_call(&r);
    msg_t m = { NULL, NULL };
    xQueueSend(queue, &m, portMAX_DELAY);
    xSemaphoreTake(stopped, portMAX_DELAY);
    return final_hwm;
}

int engine_call(req_t *req)
{
    msg_t m = { req, xTaskGetCurrentTaskHandle() };
    xQueueSend(queue, &m, portMAX_DELAY);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return req->ret;
}
