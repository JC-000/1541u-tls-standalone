/*
 * stubs.c
 *
 * The two functions tls_engine.c calls that are neither mbedTLS nor ESP-IDF. In the bridge they live in
 * wifi_modem.c and sntp.c; here nothing else of the bridge is linked.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cmd_buffer.h"
#include "wifi_modem.h"
#include "sntp.h"
#include "net.h"
#include "stubs.h"

volatile bool stub_no_entropy;
volatile bool stub_sntp_time;

// As the bridge: true entropy once the radio runs (esp_random.h). QEMU has no radio, so the open_eth
// path reports RADIO as a declared test stub (net_entropy_stubbed); its RNG is QEMU's.
entropy_source_t entropy_source(void)
{
    if (stub_no_entropy) {
        return ENTROPY_NONE;
    }
    return (net_radio_started || net_entropy_stubbed) ? ENTROPY_RADIO : ENTROPY_NONE;
}

// The app takes SNTP time and hands it to every OPEN as the Ultimate does (TLS_TIME_NIOS). Only the case that
// tests the engine's own-SNTP branch reports an SNTP clock.
int sntp_has_time(void)
{
    return stub_sntp_time;
}
