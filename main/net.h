/*
 * net.h
 *
 * Network bring-up. One image for both targets: with Wi-Fi credentials in NVS (namespace "standalone", keys
 * "ssid" and "pass") it joins that network as a station, as a dev board needs; without them it uses QEMU's
 * open_eth NIC. Then SNTP.
 */

#ifndef NET_H_
#define NET_H_

#include <stdbool.h>

extern volatile bool net_radio_started;
extern volatile bool net_entropy_stubbed;

// Returns the name of the path taken ("wifi" or "qemu-openeth"), or NULL on failure
const char *net_start(void);
// Waits for SNTP; true once the clock is set
bool net_sntp(int timeout_ms);

#endif /* NET_H_ */
