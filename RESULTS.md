# Results

## Revision 2 (after standalone-adversarial1): the current evidence
**All timings are QEMU timings, not hardware timings.** Between the two clean runs below, the same case varied by
up to 4x with host load. Use the timings to compare cases only.

| | |
|---|---|
| App | commit `d30cc89` (code); this file and the README are documentation-only commits on top of it |
| Engine | `ENGINE_SHA` f2e46946af6f091586a24cb4c2243ef8c1b4de76, unmodified (checked at configure and at build) |
| `tls_engine.c` sha256 | `5ee04a59562245dd79bf5e7f7ee5cf4e30abb76bbfc4dc2d5e9b56d680da5361` |
| `tls_engine.h` sha256 | `efabf8a2450c5f4eb7ca7a2549ef71a75e58aa191a6c41b536ea648ef333b109` |
| Image | `espressif/idf@sha256:da1f22c157dcb4f684f822ff37be17dc0abef82160d889bfd7fd23cc1299f0fa` (v6.0.3, arm64); ESP-IDF v6.0.3, mbedTLS 4.1.1, QEMU 9.2.2 (esp_develop_9.2.2_20260417), OpenSSL 3.0.13 for the servers |
| Runs (2026-10-02) | `redgreen.sh`: **clean VERDICT GREEN, mut1 VERDICT RED, mut2 VERDICT RED**, as required; plus a second clean run, GREEN |

**These results are for f2e46946's engine bytes. They must be re-run against the frozen PR 4 tip (set
`ENGINE_SHA`) before PR 4 cites them.**

| Run | gate | info | VERDICT |
|---|---|---|---|
| clean (run 1) | 15/15 | 9/9 | GREEN |
| clean (run 2) | 15/15 | 9/9 | GREEN |
| mut1 (drops plaintext after 4 KiB) | 9 pass / 6 FAIL: every download ended idle-timeout, short | 4/9 | **RED** |
| mut2 (output written before the input is consumed) | 3 pass / 12 FAIL: every handshake failed | 1/9 | **RED** |

Clean run 1:

| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B | transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | info | PASS (HTTP/1.1 200 OK, body 751148 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 4175 | 4105.984 | 751148 | 1095 | 2203.0 | 1248 (709) | 3539.363 | 51768 | 16717 | 4756 |
| github | info | PASS (HTTP/1.1 200 OK, body 16739 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 3661 | 3571.124 | 16739 | 154 | 1114.1 | 31 (2) | 2765.172 | 47084 | 16717 | 4684 |
| example | info | PASS (HTTP/1.1 200 OK, body 713 B, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 4563 | 4506.744 | 713 | 58 | - | 10 (0) | 3722.336 | 47144 | 16717 | 4748 |
| gnu-rsa2048 | info | PASS (HTTP/1.1 200 OK, body 2862 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 2720 | 2618.979 | 2862 | 481 | - | 14 (0) | 1915.412 | 51304 | 16717 | 4508 |
| debian-rsa4096 | info | PASS (HTTP/1.1 200 OK, body 15864 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1747 | 1657.104 | 15864 | 169 | 3809.2 | 39 (0) | 954.006 | 52132 | 16717 | 4476 |
| c64wiki-tls12 | info | PASS (HTTP/1.1 200 OK, body 64732 B, close-notify) | close-notify | 1.2 | 0xcca8 | X25519 | 2092 | 1689.869 | 64732 | 490 | 5533.0 | 125 (48) | 1542.639 | 49876 | 16717 | 4812 |
| craigslist-tls12 | info | PASS (HTTP/1.1 200 OK, body 2185 B = Content-Length, idle-timeout) | idle-timeout | 1.2 | 0xc02c | P-256 | 1987 | 1823.363 | 2185 | 20194 | - | 16 (0) | 1677.158 | 43644 | 16717 | 4860 |
| badssl-no-ems | info | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 417.291 | 35712 | 16717 | 6348 |
| c64wiki-require13 | info | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 834.163 | 35780 | 16717 | 6348 |
| local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 4218 | 4213.191 | 262144 | 873 | 1313.1 | 441 (240) | 3627.526 | 43848 | 16717 | 4876 |
| local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 4939 | 4918.699 | 262144 | 716 | 2015.2 | 444 (240) | 3857.635 | 40012 | 16717 | 4748 |
| local-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 1880.882 | 37584 | 16717 | 4748 |
| local-selfsigned | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 582.003 | 35800 | 16717 | 4956 |
| local-selfsigned-bundle | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 649.147 | 35800 | 16717 | 4956 |
| local-expired | gate | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 1469.349 | 37568 | 16717 | 4876 |
| local-no-ems | gate | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 568.718 | 35572 | 16717 | 6348 |
| local-require13-vs-tls12 | gate | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 481.092 | 35784 | 16717 | 6348 |
| local-pin | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2163 | 2158.159 | 262144 | 396 | 2080.5 | 437 (240) | 1613.659 | 42392 | 16717 | 4812 |
| local-pin-wrong | gate | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 454.545 | 35828 | 16717 | 4956 |
| local-no-time | gate | PASS (open 259 reason 2) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6716 |
| local-no-entropy | gate | PASS (open 259 reason 1) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6748 |
| local-sntp-clock | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2526 | 2515.522 | 262144 | 413 | 1967.3 | 438 (240) | 2145.311 | 34072 | 16717 | 4720 |
| pair-local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2619 | 2608.925 | 262144 | 4433 | 2272.5 | 435 (240) | 2156.918 | 80708 | 16717 | 4812 |
| pair-local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 2334 | 2327.470 | 262144 | 2029 | 2266.1 | 436 (240) | 2000.502 | 80708 | 16717 | 4812 |

`SUMMARY net=qemu-openeth gate_pass=15 gate_fail=0 info_pass=9 info_fail=0`  
`VERDICT GREEN`  

Clean run 2:

| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B | transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | info | PASS (HTTP/1.1 200 OK, body 751148 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 3314 | 3243.269 | 751148 | 725 | 3290.3 | 1244 (709) | 2757.098 | 53192 | 16717 | 4680 |
| github | info | PASS (HTTP/1.1 200 OK, body 16739 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1846 | 1758.762 | 16739 | 160 | 3006.0 | 31 (2) | 1417.967 | 46932 | 16717 | 4872 |
| example | info | PASS (HTTP/1.1 200 OK, body 713 B, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2022 | 1972.188 | 713 | 53 | - | 10 (0) | 1644.622 | 47732 | 16717 | 4872 |
| gnu-rsa2048 | info | PASS (HTTP/1.1 200 OK, body 2862 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 1548 | 1454.564 | 2862 | 144 | - | 15 (0) | 1157.809 | 51252 | 16717 | 4504 |
| debian-rsa4096 | info | PASS (HTTP/1.1 200 OK, body 15864 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1810 | 1608.045 | 15864 | 450 | 3606.2 | 43 (0) | 952.266 | 52116 | 16717 | 4472 |
| c64wiki-tls12 | info | PASS (HTTP/1.1 200 OK, body 64732 B, close-notify) | close-notify | 1.2 | 0xcca8 | X25519 | 1949 | 1556.047 | 64732 | 479 | 5683.8 | 124 (46) | 1416.553 | 49884 | 16717 | 4808 |
| craigslist-tls12 | info | PASS (HTTP/1.1 200 OK, body 2185 B = Content-Length, idle-timeout) | idle-timeout | 1.2 | 0xc02c | P-256 | 2087 | 1933.403 | 2185 | 20133 | - | 16 (0) | 1797.779 | 43664 | 16717 | 4856 |
| badssl-no-ems | info | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 130.158 | 38684 | 16717 | 6392 |
| c64wiki-require13 | info | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 132.431 | 37092 | 16717 | 6344 |
| local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1101 | 1097.724 | 262144 | 197 | 3509.7 | 434 (240) | 952.809 | 42300 | 16717 | 4872 |
| local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1205 | 1201.529 | 262144 | 181 | 5189.6 | 436 (240) | 998.029 | 39920 | 16717 | 4920 |
| local-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 464.938 | 37576 | 16717 | 4952 |
| local-selfsigned | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 164.172 | 35752 | 16717 | 4952 |
| local-selfsigned-bundle | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 170.807 | 35744 | 16717 | 4952 |
| local-expired | gate | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 635.508 | 37576 | 16717 | 4760 |
| local-no-ems | gate | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 192.326 | 35540 | 16717 | 6344 |
| local-require13-vs-tls12 | gate | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 165.114 | 37108 | 16717 | 6344 |
| local-pin | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 672 | 669.309 | 262144 | 132 | 4826.3 | 434 (240) | 503.605 | 42296 | 16717 | 4808 |
| local-pin-wrong | gate | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 166.042 | 37216 | 16717 | 4952 |
| local-no-time | gate | PASS (open 259 reason 2) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6600 |
| local-no-entropy | gate | PASS (open 259 reason 1) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6744 |
| local-sntp-clock | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 929 | 925.576 | 262144 | 130 | 4915.4 | 434 (240) | 767.725 | 42324 | 16717 | 4872 |
| pair-local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 949 | 946.702 | 262144 | 1456 | 4642.9 | 434 (240) | 794.423 | 81640 | 16717 | 4856 |
| pair-local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1171 | 1167.978 | 262144 | 281 | 4759.9 | 435 (240) | 1014.197 | 81640 | 16717 | 4856 |

`SUMMARY net=qemu-openeth gate_pass=15 gate_fail=0 info_pass=9 info_fail=0`  
`VERDICT GREEN`  

### Readings
- **Every gate download is complete.** Each one is 262,144 B with the expected SHA-256 and ends with close_notify,
  including the two downloads interleaved step by step.
- **Every public download is complete:** its body matches Content-Length where the server sent one. Craigslist is
  the one accepted idle end, and its Content-Length matches.
- **NO_MEM back-pressure is routine.** A 256 KiB download takes 434-444 steps, and 240 of them are resends after
  `ESP_ERR_NO_MEM` (`again_steps`). So the resend path is exercised on every bulk transfer.
- **Stack:** the worst is **4,472 B free of 8,192** (www.debian.org, RSA-4096, run 2). This is the worst of the
  cases run, measured with the bridge's handlers on the stack, but not its dispatcher frames.
- **Heap** (QEMU, **no Wi-Fi driver**):
  - one session uses 34-53 KB;
  - two sessions interleaved use 80.7-81.6 KB;
  - the largest single allocation on the TLS task is 16,717 B in every case that reached the handshake.

  Only the per-session use carries over to the firmware.
- **The engine's other branches ran:**
  - the stub flipped to "no entropy" gives OPEN refused, reason 1 NO_ENTROPY;
  - the stub flipped to an SNTP clock gives time source 2 SNTP, and the download is checked.

## Revision 1 (superseded)
Runs at `f5fa967`/`bd1b850`, 22/22 PASS. That oracle did not check byte counts, hashes or how a session ended,
and an engine that dropped 99.5% of the data also passed (standalone-adversarial1 BLOCKER-1). The numbers are
kept in the archive's `superseded-r1/` for history only.
