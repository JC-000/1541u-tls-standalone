/*
 * engine_task.h
 *
 * The engine on a task of its own, as in the bridge (rpc_dispatch.c): every tls_* call runs on the "TLS" task,
 * 8,192 B of stack, pinned to core 1, at tskIDLE_PRIORITY + 1. A caller hands it one request and blocks until
 * it is done, which is what an RPC from the Ultimate amounts to, minus the UART.
 */

#ifndef ENGINE_TASK_H_
#define ENGINE_TASK_H_

#include <stdint.h>
#include "tls_engine.h"

#define TLS_TASK_STACK 8192 // rpc_dispatch.h at f2e46946

typedef enum { REQ_OPEN, REQ_STEP, REQ_CLOSE, REQ_STATS, REQ_SET_CA, REQ_CLOSE_ALL } req_cmd_t;

typedef struct {
    req_cmd_t cmd;
    uint8_t slot;
    uint16_t tag;
    const tls_open_params_t *open;   // OPEN
    uint8_t reason;                  // OPEN
    uint8_t flags;                   // STEP
    const uint8_t *ct_in;            // STEP
    int ct_in_len;
    const uint8_t *pt_in;
    int pt_in_len;
    uint8_t out[TLS_STEP_OUT_MAX];   // STEP: ct_out then pt_out; CLOSE: close_notify
    uint16_t ct_out_len;
    uint16_t pt_out_len;
    tls_step_result_t result;        // STEP
    const uint8_t *der;              // SET_CA
    int der_len;
    uint16_t ca_tag;
    uint8_t reject;
    tls_session_stats_t stats;       // STATS, with the heap figures cmd_tls_stats() reports
    uint32_t heap_free;
    uint32_t heap_min;
    uint32_t largest_block;
    uint32_t stack_free;
    int ret;
    uint32_t call_us;                // how long the engine call took on the TLS task
} req_t;

// A new TLS task, so its stack high-water mark covers only what runs from now on
void engine_task_start(void);
// Ends the TLS task (after tls_close_all()); returns its final stack high-water mark in bytes
uint32_t engine_task_stop(void);
int engine_call(req_t *req);

// Largest single allocation made on the TLS task since engine_alloc_reset() (heap hook)
void engine_alloc_reset(void);
uint32_t engine_alloc_max(void);

#endif /* ENGINE_TASK_H_ */
