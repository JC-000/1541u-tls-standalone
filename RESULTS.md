# Results

**All timings are QEMU timings, not hardware timings.** The same case varied by up to 4.4x between earlier runs
on the same host, with host load. Use the timings to compare cases only.

## Current: revision 3
| | |
|---|---|
| App | commit `db07593` (code); later commits change documentation only |
| Engine | `ENGINE_SHA` f2e46946af6f091586a24cb4c2243ef8c1b4de76, unmodified (checked at configure and at every build) |
| `tls_engine.c` sha256 | `5ee04a59562245dd79bf5e7f7ee5cf4e30abb76bbfc4dc2d5e9b56d680da5361` |
| `tls_engine.h` sha256 | `efabf8a2450c5f4eb7ca7a2549ef71a75e58aa191a6c41b536ea648ef333b109` |
| Hand copies | `tools/verify_transcriptions.py`: `rpc_dispatch.c`, `wifi_tls.cc` and `network_target.cc` at that commit equal their references, and the copies differ only by the declared deltas |
| Image | `espressif/idf@sha256:da1f22c157dcb4f684f822ff37be17dc0abef82160d889bfd7fd23cc1299f0fa` (v6.0.3, arm64); ESP-IDF v6.0.3, mbedTLS 4.1.1, QEMU 9.2.2 (esp_develop_9.2.2_20260417), OpenSSL 3.0.13 and Python for the servers |
| `tls_standalone.bin` | `d31f2d0a5a9b2b7d741e21838fb1a728d108e1d965e88d42807f822d34302449`, **identical in both clean runs** (reproducible build, `SOURCE_DATE_EPOCH` = the engine commit's time) |

**These results are for f2e46946's engine bytes.** Any other engine commit, including the one in the engine PR,
must be run again before it is cited.

| Run (2026-10-02) | gate | info | VERDICT | required |
|---|---|---|---|---|
| clean | 18/18 | 9/9 | GREEN | GREEN: ok |
| clean, second run | 18/18 | 9/9 | GREEN | GREEN: ok |
| mut1: drops plaintext after 4 KiB | 12/18 | 5/9 | RED | RED: ok |
| mut2: output written before the input is consumed | 3/18 | 1/9 | RED | RED: ok |
| m3b: last record before close_notify dropped | 12/18 | 5/9 | RED | RED: ok |
| m4b: no close_notify from `tls_close()` | 17/18 (local-close-established FAIL) | 9/9 | RED | RED: ok |
| m5: one bit flipped at 128 KiB | 12/18 | 9/9 | RED | RED: ok |
| m6b: wrong host name accepted under BUNDLE | 17/18 (bundle-wronghost FAIL) | 9/9 | RED | RED: ok |
| m10: writes truncated to 512 B | 17/18 (local-post-8k FAIL) | 9/9 | RED | RED: ok |
| m8c: handshake skipped on a refused step | 18/18 | 9/9 | GREEN | **known gap, not required** (see the README's Limits) |

m5 passes every info case: public bodies are checked by length only.

Clean run:

| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B | transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | info | PASS (HTTP/1.1 200 OK, body 751148 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1578 | 1522.686 | 751148 | 383 | 7614.6 | 1255 (673) | 1295.956 | 53292 | 16717 | 4876 |
| github | info | PASS (HTTP/1.1 200 OK, body 16739 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1105 | 1022.267 | 16739 | 156 | 3940.9 | 33 (1) | 803.474 | 46936 | 16717 | 4876 |
| example | info | PASS (HTTP/1.1 200 OK, body 713 B, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1391 | 1346.480 | 713 | 46 | - | 10 (0) | 1132.307 | 47732 | 16717 | 4876 |
| gnu-rsa2048 | info | PASS (HTTP/1.1 200 OK, body 2862 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 1135 | 1034.134 | 2862 | 103 | - | 15 (0) | 816.653 | 51636 | 16717 | 4496 |
| debian-rsa4096 | info | PASS (HTTP/1.1 200 OK, body 15864 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1274 | 1163.667 | 15864 | 239 | 4808.3 | 43 (0) | 678.561 | 52252 | 16717 | 4464 |
| c64wiki-tls12 | info | PASS (HTTP/1.1 200 OK, body 64732 B, close-notify) | close-notify | 1.2 | 0xcca8 | X25519 | 1508 | 1129.236 | 64732 | 480 | 7016.1 | 125 (47) | 1018.335 | 49876 | 16717 | 4800 |
| craigslist-tls12 | info | PASS (HTTP/1.1 200 OK, body 2185 B = Content-Length, idle-timeout) | idle-timeout | 1.2 | 0xc02c | P-256 | 1397 | 1240.661 | 2185 | 20139 | - | 16 (0) | 1133.069 | 43672 | 16717 | 4848 |
| badssl-no-ems | info | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 108.799 | 38712 | 16717 | 6336 |
| c64wiki-require13 | info | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 107.839 | 37104 | 16717 | 6384 |
| bundle-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 5 (0) | 1102.585 | 47976 | 16717 | 4672 |
| local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 635 | 633.559 | 262144 | 82 | 7687.7 | 434 (240) | 527.098 | 42308 | 16717 | 4956 |
| local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 755 | 752.788 | 262144 | 122 | 7570.8 | 437 (240) | 645.635 | 41484 | 16717 | 4960 |
| local-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 323.294 | 37568 | 16717 | 4880 |
| local-selfsigned | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 115.796 | 37212 | 16717 | 4944 |
| local-selfsigned-bundle | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 116.640 | 37212 | 16717 | 4944 |
| local-expired | gate | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 329.243 | 37568 | 16717 | 4944 |
| local-no-ems | gate | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 106.831 | 35668 | 16717 | 6352 |
| local-require13-vs-tls12 | gate | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 107.281 | 37232 | 16717 | 6336 |
| local-pin | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 423 | 421.215 | 262144 | 81 | 7703.0 | 434 (240) | 315.762 | 42304 | 16717 | 4944 |
| local-pin-wrong | gate | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 116.440 | 37228 | 16717 | 4944 |
| local-post-8k | gate | PASS (HTTP/1.0 200 OK, body 65 B = Content-Length, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 638 | 636.177 | 65 | 44 | - | 29 (0) | 529.249 | 40656 | 16717 | 4496 |
| local-close-established | gate | PASS (HTTP/1.1 200 OK, body 5 B = Content-Length, closed-by-client, server saw close_notify) | closed-by-client | 1.3 | 0x1302 | X25519 | 648 | 645.971 | 5 | 2 | - | 7 (0) | 537.854 | 39104 | 16717 | 4496 |
| local-no-time | gate | PASS (open 259 reason 2) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6624 |
| local-no-entropy | gate | PASS (open 259 reason 1) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6736 |
| local-sntp-clock | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 633 | 631.095 | 262144 | 82 | 7765.1 | 434 (240) | 525.735 | 42304 | 16717 | 4864 |
| pair-local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 652 | 649.828 | 262144 | 925 | 7639.3 | 434 (240) | 540.938 | 80932 | 16717 | 4864 |
| pair-local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 755 | 753.352 | 262144 | 168 | 7833.1 | 435 (240) | 644.355 | 80932 | 16717 | 4864 |

`SUMMARY net=qemu-openeth gate_pass=18 gate_fail=0 info_pass=9 info_fail=0`  
`VERDICT GREEN`  

Second clean run:

| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B | transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | info | PASS (HTTP/1.1 200 OK, body 751148 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1536 | 1477.257 | 751148 | 381 | 7255.3 | 1258 (695) | 1243.842 | 54748 | 16717 | 4860 |
| github | info | PASS (HTTP/1.1 200 OK, body 16739 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1082 | 1004.043 | 16739 | 119 | 3073.3 | 32 (1) | 777.008 | 48552 | 16717 | 4676 |
| example | info | PASS (HTTP/1.1 200 OK, body 713 B, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1372 | 1327.684 | 713 | 49 | - | 10 (0) | 1109.274 | 47568 | 16717 | 4868 |
| gnu-rsa2048 | info | PASS (HTTP/1.1 200 OK, body 2862 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 1116 | 1018.902 | 2862 | 147 | - | 16 (0) | 786.814 | 51632 | 16717 | 4500 |
| debian-rsa4096 | info | PASS (HTTP/1.1 200 OK, body 15864 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1248 | 1153.268 | 15864 | 370 | 2077.5 | 41 (0) | 660.827 | 52280 | 16717 | 4468 |
| c64wiki-tls12 | info | PASS (HTTP/1.1 200 OK, body 64732 B, close-notify) | close-notify | 1.2 | 0xcca8 | X25519 | 1542 | 1153.882 | 64732 | 478 | 3340.5 | 121 (48) | 1016.007 | 49924 | 16717 | 4804 |
| craigslist-tls12 | info | PASS (HTTP/1.1 200 OK, body 2185 B = Content-Length, idle-timeout) | idle-timeout | 1.2 | 0xc02c | P-256 | 1409 | 1239.550 | 2185 | 20148 | - | 16 (0) | 1105.490 | 43640 | 16717 | 4852 |
| badssl-no-ems | info | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 113.334 | 38712 | 16717 | 6396 |
| c64wiki-require13 | info | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 125.237 | 37116 | 16717 | 6340 |
| bundle-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 5 (0) | 1048.151 | 47976 | 16717 | 4868 |
| local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 624 | 623.094 | 262144 | 81 | 7679.2 | 434 (240) | 516.762 | 42260 | 16717 | 4876 |
| local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 743 | 740.918 | 262144 | 118 | 7593.5 | 436 (240) | 634.218 | 39904 | 16717 | 4980 |
| local-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 314.052 | 37580 | 16717 | 4948 |
| local-selfsigned | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 111.149 | 37232 | 16717 | 4948 |
| local-selfsigned-bundle | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 114.170 | 37232 | 16717 | 4948 |
| local-expired | gate | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 324.230 | 37580 | 16717 | 4868 |
| local-no-ems | gate | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 105.729 | 37108 | 16717 | 6340 |
| local-require13-vs-tls12 | gate | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 106.859 | 37200 | 16717 | 6340 |
| local-pin | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 421 | 420.123 | 262144 | 82 | 7595.8 | 434 (240) | 315.214 | 42280 | 16717 | 4948 |
| local-pin-wrong | gate | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 114.664 | 37216 | 16717 | 4948 |
| local-post-8k | gate | PASS (HTTP/1.0 200 OK, body 65 B = Content-Length, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 620 | 618.624 | 65 | 52 | - | 29 (0) | 515.839 | 42200 | 16717 | 4500 |
| local-close-established | gate | PASS (HTTP/1.1 200 OK, body 5 B = Content-Length, closed-by-client, server saw close_notify) | closed-by-client | 1.3 | 0x1302 | X25519 | 632 | 630.629 | 5 | 1 | - | 6 (0) | 525.286 | 42244 | 16717 | 4500 |
| local-no-time | gate | PASS (open 259 reason 2) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6724 |
| local-no-entropy | gate | PASS (open 259 reason 1) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6740 |
| local-sntp-clock | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 620 | 618.653 | 262144 | 84 | 7553.9 | 434 (240) | 514.571 | 42292 | 16717 | 4948 |
| pair-local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 640 | 639.100 | 262144 | 919 | 7286.6 | 434 (240) | 531.977 | 80524 | 16717 | 4868 |
| pair-local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 748 | 746.179 | 262144 | 169 | 7446.2 | 435 (240) | 637.612 | 80524 | 16717 | 4868 |

`SUMMARY net=qemu-openeth gate_pass=18 gate_fail=0 info_pass=9 info_fail=0`  
`VERDICT GREEN`  

### Readings
- **Gate downloads are complete.**
  - Every 256 KiB download is 262,144 B with the expected SHA-256, ending in close_notify; the pair is interleaved step by step.
  - The 8 KiB POST (nine 896 B WRITEs and one of 128 B: whole 32 B blocks, rounded up past `WRITE_CHUNK` 892) reaches the server intact: the server's SHA-256 of what it received matches.
  - CLOSE on an ESTABLISHED session delivers a close_notify that the server reports.
- **The BUNDLE name check fails closed:** en.wikipedia.org under the name `nomatch.invalid` fails with CERT_VERIFY, verify 0x04.
- **NO_MEM resends on the READ side are routine:** 240 of the 434-437 steps of every 256 KiB download. Handshake-side and write-side resends never happen here.
- **Stack:** the worst is **4,464 B free of 8,192** (www.debian.org, RSA-4096). This is the worst of the cases run, with the bridge's TLS-task call chain except the UART send.
- **Heap** (QEMU, **no Wi-Fi driver**):
  - a successful single session uses 39.1-54.7 KB, and a failed handshake 35.7-48.0 KB;
  - two sessions interleaved use 80.5-80.9 KB;
  - the largest single allocation on the TLS task is 16,717 B in every case that reached the handshake.

  Only the per-session use carries over to the firmware.

## Revision 2 (superseded by revision 3)
Clean runs were GREEN (gate 15/15), mut1 and mut2 were RED (mut1: gate 9/15, info 5/9; mut2: gate 3/15, info
1/9). An internal review then found that four further mutants stayed GREEN; three of them are now required reds
(m4b, m6b, m10), and m8c is a disclosed gap.

Two corrections to what revision 2 said:
- **Heap.** Its low single-session figure, 34.1 KB (`local-sntp-clock`, run 1), was an artifact: the baseline was
  taken before the CA upload, and the engine restarts its heap minimum at OPEN, so a transient freed in between
  shrank the figure. The same case measured 42.3 KB in the other run.
- **Timing swing.** "Up to 4x" understated it: it was up to 4.4x.

## Revision 1 (superseded)
22/22 PASS, but that oracle did not check byte counts, hashes or how a session ended. An internal review showed
that an engine dropping 99.5% of the data also passed.
