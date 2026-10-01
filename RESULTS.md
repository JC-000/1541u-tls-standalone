# Results

**All timings below are QEMU timings, not hardware timings.** The runs used `qemu-system-xtensa -M esp32`
(QEMU 9.2.2, esp_develop_9.2.2_20260417) on an Apple-silicon host under Colima. QEMU does not model the ESP32's
240/160 MHz clock or its memory wait states, so a handshake or transfer time here says nothing about a real
ESP32. Use them only to compare cases with each other. The heap and stack figures come from the same code and
allocator as on a device. Interrupt nesting on silicon can add stack.

- Runs: run2, at repo commit `bd1b850`, 2026-10-01 23:22Z: 22/22 PASS. run1, at `f5fa967`, gave the same 22/22;
  the only code difference is www.debian.org's path.
- Engine: `tls_engine.c` sha256 `5ee04a59…5da5361` and `tls_engine.h` `efabf8a2…e333b109`, from f2e46946.
- Stack: ESP-IDF v6.0.3, mbedTLS 4.1.1, OpenSSL 3.0.13 for the local servers.
- Network: QEMU user-mode networking (open_eth). The public hosts are reached through the internet.
- Evidence: `session-1001/evidence/standalone/run{1,2}/` in the archive: the full QEMU log, the build log, the
  sdkconfig diff, the image sha256 values and the server controls.

Column notes:
- *Handshake wall*: from OPEN to ESTABLISHED, including network round trips.
- *mbedTLS hs*: the engine's own `handshake_us`, the time spent inside `mbedtls_ssl_handshake_step`.
- *KiB/s engine*: plaintext bytes divided by the time spent inside `tls_step` during the transfer.
- *Longest step*: the longest single engine call, which is how long the TLS task is blocked.
- *Heap peak use*: internal 8-bit free heap before the case, minus its minimum during the case (the engine
  restarts the minimum monitor at the first OPEN). It includes lwIP's buffers.
- *Largest alloc*: the largest single allocation made on the TLS task (heap hook).
- *Stack free*: `uxTaskGetStackHighWaterMark` of the 8,192 B TLS task, which is started fresh for each case.

| case | verdict | outcome | TLS | suite | group | handshake wall ms | mbedTLS hs ms | plaintext B | transfer ms | KiB/s wall | KiB/s engine | longest step ms | heap peak use B | heap min free B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | PASS (HTTP/1.1 200 OK) | close-notify | 1.3 | 0x1303 | X25519 | 1571 | 1515.174 | 757578 | 368 | 2010.4 | 9061.9 | 1298.555 | 53208 | 166608 | 16717 | 4980 |
| github | PASS (HTTP/1.1 200 OK) | close-notify | 1.3 | 0x1303 | X25519 | 1038 | 955.905 | 17884 | 162 | 107.8 | 5769.7 | 749.298 | 46916 | 171600 | 16717 | 4980 |
| example | PASS (HTTP/1.1 200 OK) | close-notify | 1.3 | 0x1303 | X25519 | 1402 | 1354.896 | 1068 | 48 | - | - | 1145.067 | 47724 | 170772 | 16717 | 4980 |
| gnu-rsa2048 | PASS (HTTP/1.1 200 OK) | close-notify | 1.3 | 0x1302 | X25519 | 1107 | 1012.101 | 3291 | 131 | - | - | 804.998 | 51288 | 167200 | 16717 | 4612 |
| debian-rsa4096 | PASS (HTTP/1.1 200 OK) | close-notify | 1.3 | 0x1303 | X25519 | 1267 | 1177.694 | 16557 | 174 | 92.9 | 6228.4 | 695.345 | 51824 | 166676 | 16717 | 4580 |
| c64wiki-tls12 | PASS (HTTP/1.1 200 OK) | close-notify | 1.2 | 0xcca8 | X25519 | 1492 | 1104.692 | 65442 | 467 | 136.8 | 9236.6 | 1002.477 | 49932 | 168568 | 16717 | 4916 |
| craigslist-tls12 | PASS (HTTP/1.1 200 OK) | idle-timeout | 1.2 | 0xc02c | P-256 | 1439 | 1239.599 | 3321 | 20103 | - | - | 1135.159 | 43664 | 174836 | 16717 | 4964 |
| badssl-no-ems | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed (reason 3, verify 0x00000000) | - | 0x0000 | - | - | - | 0 | - | - | - | 100.712 | 38808 | 179468 | 16717 | 6452 |
| c64wiki-require13 | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | - | 103.538 | 37104 | 181152 | 16717 | 6500 |
| local-tls13 | PASS (HTTP/1.0 200 ok) | close-notify | 1.3 | 0x1303 | X25519 | 604 | 602.210 | 262189 | 71 | 3606.3 | 9338.9 | 499.858 | 42256 | 176000 | 16717 | 4980 |
| local-tls12 | PASS (HTTP/1.0 200 ok) | close-notify | 1.2 | 0xcca9 | X25519 | 776 | 773.394 | 262189 | 76 | 3369.0 | 8739.3 | 661.886 | 39916 | 178160 | 16717 | 4964 |
| local-wronghost | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed (reason 4, verify 0x00000004) | - | 0x0000 | - | - | - | 0 | - | - | - | 318.217 | 37608 | 180256 | 16717 | 4980 |
| local-selfsigned | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed (reason 4, verify 0x00000008) | - | 0x0000 | - | - | - | 0 | - | - | - | 113.568 | 35816 | 181824 | 16717 | 5060 |
| local-selfsigned-bundle | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed (reason 4, verify 0x00000008) | - | 0x0000 | - | - | - | 0 | - | - | - | 115.847 | 35764 | 181656 | 16717 | 5060 |
| local-expired | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed (reason 4, verify 0x00000001) | - | 0x0000 | - | - | - | 0 | - | - | - | 311.441 | 37556 | 179664 | 16717 | 5060 |
| local-no-ems | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed (reason 3, verify 0x00000000) | - | 0x0000 | - | - | - | 0 | - | - | - | 101.819 | 35544 | 181464 | 16717 | 6404 |
| local-require13-vs-tls12 | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | - | 101.825 | 37084 | 179712 | 16717 | 6452 |
| local-pin | PASS (HTTP/1.0 200 ok) | close-notify | 1.3 | 0x1303 | X25519 | 405 | 403.712 | 262189 | 67 | 3821.6 | 9519.8 | 302.637 | 42284 | 174512 | 16717 | 5060 |
| local-pin-wrong | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed (reason 5, verify 0x00000008) | - | 0x0000 | - | - | - | 0 | - | - | - | 109.889 | 37208 | 179376 | 16717 | 5060 |
| local-no-time | PASS (open 259 reason 2) | open-refused | - | 0x0000 | - | - | - | 0 | - | - | - | 0.000 | - | - | 0 | 6756 |
| pair-wikipedia | PASS (HTTP/1.1 200 OK) | close-notify | 1.3 | 0x1303 | X25519 | 1498 | 1447.658 | 757578 | 369 | 2004.9 | 9266.3 | 1240.908 | 87708 | 128664 | 16717 | 4980 |
| pair-local-tls13 | PASS (HTTP/1.0 200 ok) | close-notify | 1.3 | 0x1303 | X25519 | 610 | 608.024 | 262189 | 73 | 3507.5 | 9107.0 | 505.609 | 87708 | 128664 | 16717 | 4980 |

Readings:
- Stack:
  - the worst case is www.debian.org (RSA-4096): **4,580 B free of 8,192** (3,612 used), the same in both runs;
  - the failing handshakes and the refused OPEN use less.
- Heap:
  - one session peaks at 35.5-53.2 KB;
  - two sessions at once (the engine's maximum, one of them on a 750 KB download) peak at 84.5-87.7 KB, leaving at least 128,664 B free;
  - the largest single allocation is 16,717 B in every case that reached the handshake (mbedTLS's 16 KB input record buffer, inferred from `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`).
- www.craigslist.org kept the connection open after its reply despite `Connection: close`, so that case ended on the app's 20 s idle timeout after the whole reply arrived. This is not an engine failure.
- The TLS 1.2 failures to public hosts are by design:
  - badssl.com does not negotiate the extended master secret, which the engine requires (fail_reason 3);
  - REQUIRE_TLS13 against a TLS 1.2-only server ends in the server's alert 70 (protocol_version).
