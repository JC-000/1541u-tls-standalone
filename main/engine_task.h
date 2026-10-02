// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * engine_task.h
 *
 * The engine on a task of its own, as in the bridge (rpc_dispatch.c): every tls_* call runs on the "TLS" task,
 * 8,192 B of stack, pinned to core 1, at tskIDLE_PRIORITY + 1. A request is a command_buf_t laid out as the
 * UART RPC carries it (rpc_calls.h), and the reply overwrites it in the same buffer, as the bridge's cmd_tls_*()
 * handlers do. Only the UART is missing.
 */

#ifndef ENGINE_TASK_H_
#define ENGINE_TASK_H_

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cmd_buffer.h"
#include "tls_engine.h"

#define TLS_TASK_STACK 8192 // TLS_TASK_STACK in the bridge's rpc_dispatch.h

// A new TLS task, so its stack high-water mark covers only what runs from now on
void engine_task_start(void);
// Ends the TLS task (after a CLOSE_ALL); returns its final stack high-water mark in bytes
uint32_t engine_task_stop(void);
// Runs one request on the TLS task and returns when its reply is in buf; *call_us: time spent in the handler
void engine_call(command_buf_t *buf, uint32_t *call_us);

// Largest single allocation made on the TLS task since engine_alloc_reset() (heap hook)
void engine_alloc_reset(void);
uint32_t engine_alloc_max(void);

#endif /* ENGINE_TASK_H_ */
