# Results

**The tables under *Current* are QEMU runs, and their timings are not hardware timings.** The same case has varied
by up to 4.4x between QEMU runs on the same host, with host load; use them only to compare cases. *On a classic
ESP32 dev board* below has hardware timings.

## Current
| | |
|---|---|
| Engine | `ENGINE_SHA` 756b77fd3436dfdde26e0ec620d31fcd5401a013, on the `esp32-tls-offload` branch of https://github.com/JC-000/1541ultimate.git (also tag `esp32-tls-offload-v1`); unmodified (checked at configure and at every build) |
| `tls_engine.c` sha256 | `7eca3394476cde5797c705be87dd3453e7205e61f09b63bee82c1eba56644b9e` |
| `tls_engine.h` sha256 | `4555ce75501aefe0277417e6c0646e69154b35ccd2ee502bd6003d8eec0bab9a` |
| Hand copies | `tools/verify_transcriptions.py`: ok. `rpc_dispatch.c`, `wifi_tls.cc` and `network_target.cc` equal their references, the copies differ only by the declared deltas, and the 5 copied constants match |
| Container image | `espressif/idf@sha256:da1f22c157dcb4f684f822ff37be17dc0abef82160d889bfd7fd23cc1299f0fa` (v6.0.3, arm64): ESP-IDF v6.0.3, mbedTLS 4.1.1, QEMU 9.2.2 (esp_develop_9.2.2_20260417); OpenSSL 3.0.13 and Python for the servers |
| `tls_standalone.bin` | `afcc36eab64e59f13fe79b118df78a13442641e9df2d1efd265c36784b6f5d6d`, the same in four builds: the working repo, a fresh clone, a clone of the published branch only, and a fresh clone whose engine was fetched from the public URL, and the dev-board build (identical: the board ran this image) |

**What the image hash depends on.** It is a function of this repository's sources, the engine commit, the container
image digest and the architecture (arm64 here). The app version is fixed (`PROJECT_VER`) and the build is
reproducible (`SOURCE_DATE_EPOCH` = the engine commit's time), so the hash does not depend on this repository's own
commit.

Any other engine commit must be run again before it is cited.

| Run (2026-10-02) | gate | info | VERDICT | required |
|---|---|---|---|---|
| clean | 18/18 | 9/9 | GREEN | GREEN: ok |
| clean, from a fresh clone | 18/18 | 9/9 | GREEN | GREEN: ok |
| clean, fresh clone, engine fetched from the public URL (no override) | 18/18 | 9/9 | GREEN | GREEN: ok |
| mut1: drops plaintext after 4 KiB | 12/18 | 5/9 | RED | RED: ok |
| mut2: output written before the input is consumed | 3/18 | 1/9 | RED | RED: ok |
| m3b: last record before close_notify dropped | 12/18 | 4/9 | RED | RED: ok |
| m4b: no close_notify from `tls_close()` | 17/18 (local-close-established FAILs: the server saw no close_notify) | 9/9 | RED | RED: ok |
| m5: one bit flipped at 128 KiB | 12/18 (SHA-256 differs) | 9/9 | RED | RED: ok |
| m6b: wrong host name accepted under BUNDLE | 17/18 (bundle-wronghost FAILs: the handshake succeeded) | 9/9 | RED | RED: ok |
| m10: writes truncated to 512 B | 17/18 (local-post-8k FAILs: idle-timeout) | 9/9 | RED | RED: ok |

m5 passes every info case, because public bodies are checked by length only. The info counts of the mutants vary
a little with the public hosts' replies (m3b gave 5/9 in an earlier run).

Without Docker, all as required:
- `tools/drift_check.py`: 7 drift mutants FAIL the transcription check, and the comment-only control passes;
- `tools/selftest_redgreen.sh`: a failed build, a mutant that did not apply, and another engine commit all give
  WRONG over an older result; without python3, redgreen stops.

m8c (the handshake skipped on a refused step) is a known gap that no case catches; see the README's Limits.

Clean run:

| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B | transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | info | PASS (HTTP/1.1 200 OK, body 751148 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 3029 | 2966.133 | 751148 | 868 | 3175.0 | 1248 (709) | 2583.224 | 53188 | 16717 | 4872 |
| github | info | PASS (HTTP/1.1 200 OK, body 16739 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 3245 | 3159.577 | 16739 | 184 | 1574.4 | 31 (3) | 2424.105 | 48512 | 16717 | 4728 |
| example | info | PASS (HTTP/1.1 200 OK, body 713 B, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 3540 | 3491.014 | 713 | 53 | - | 10 (0) | 2924.722 | 47080 | 16717 | 4680 |
| gnu-rsa2048 | info | PASS (HTTP/1.1 200 OK, body 2862 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 2438 | 2337.266 | 2862 | 149 | - | 15 (0) | 1845.597 | 51596 | 16717 | 4504 |
| debian-rsa4096 | info | PASS (HTTP/1.1 200 OK, body 15864 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2832 | 2739.708 | 15864 | 171 | 2874.8 | 39 (0) | 1566.059 | 52192 | 16717 | 4472 |
| c64wiki-tls12 | info | PASS (HTTP/1.1 200 OK, body 64732 B, close-notify) | close-notify | 1.2 | 0xcca8 | X25519 | 2973 | 2566.923 | 64732 | 511 | 3818.0 | 120 (48) | 2371.178 | 49880 | 16717 | 4808 |
| craigslist-tls12 | info | PASS (HTTP/1.1 200 OK, body 2185 B = Content-Length, idle-timeout) | idle-timeout | 1.2 | 0xc02c | P-256 | 3967 | 3805.968 | 2185 | 20141 | - | 17 (0) | 1978.823 | 45232 | 16717 | 4840 |
| badssl-no-ems | info | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 223.687 | 38712 | 16717 | 6344 |
| c64wiki-require13 | info | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 263.344 | 37072 | 16717 | 6344 |
| bundle-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 5 (0) | 2075.979 | 47964 | 16717 | 4872 |
| local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1129 | 1124.733 | 262144 | 152 | 4482.6 | 434 (240) | 933.754 | 42300 | 16717 | 4872 |
| local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1388 | 1382.226 | 262144 | 217 | 4464.7 | 436 (240) | 1176.056 | 39924 | 16717 | 4856 |
| local-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 594.986 | 37572 | 16717 | 4872 |
| local-selfsigned | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 204.324 | 37200 | 16717 | 4952 |
| local-selfsigned-bundle | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 212.520 | 35748 | 16717 | 4952 |
| local-expired | gate | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 595.863 | 37564 | 16717 | 4872 |
| local-no-ems | gate | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 197.793 | 35548 | 16717 | 6392 |
| local-require13-vs-tls12 | gate | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 202.459 | 35772 | 16717 | 6344 |
| local-pin | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 771 | 768.846 | 262144 | 153 | 4509.9 | 434 (240) | 576.783 | 42304 | 16717 | 4952 |
| local-pin-wrong | gate | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 211.703 | 35768 | 16717 | 4952 |
| local-post-8k | gate | PASS (HTTP/1.0 200 OK, body 65 B = Content-Length, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 1150 | 1146.903 | 65 | 43 | - | 29 (0) | 959.304 | 42184 | 16717 | 4504 |
| local-close-established | gate | PASS (HTTP/1.1 200 OK, body 5 B = Content-Length, closed-by-client, server saw close_notify) | closed-by-client | 1.3 | 0x1302 | X25519 | 1179 | 1175.997 | 5 | 2 | - | 6 (0) | 979.244 | 40656 | 16717 | 4504 |
| local-no-time | gate | PASS (open 259 reason 2) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6536 |
| local-no-entropy | gate | PASS (open 259 reason 1) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6744 |
| local-sntp-clock | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1168 | 1164.703 | 262144 | 154 | 4523.5 | 434 (240) | 966.391 | 42388 | 16717 | 4676 |
| pair-local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1163 | 1160.252 | 262144 | 1769 | 4092.1 | 434 (240) | 965.891 | 80276 | 16717 | 4724 |
| pair-local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1438 | 1432.900 | 262144 | 327 | 4216.3 | 435 (240) | 1228.109 | 80276 | 16717 | 4724 |

`SUMMARY net=qemu-openeth gate_pass=18 gate_fail=0 info_pass=9 info_fail=0`  
`VERDICT GREEN`  

Clean run from a fresh clone:

| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B | transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | info | PASS (HTTP/1.1 200 OK, body 751148 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2810 | 2749.343 | 751148 | 580 | 4238.0 | 1248 (706) | 2366.630 | 53188 | 16717 | 4676 |
| github | info | PASS (HTTP/1.1 200 OK, body 16739 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2049 | 1970.670 | 16739 | 188 | 2250.4 | 31 (3) | 1580.472 | 48516 | 16717 | 4676 |
| example | info | PASS (HTTP/1.1 200 OK, body 577 B, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2985 | 2937.905 | 577 | 50 | - | 9 (0) | 2549.233 | 47088 | 16717 | 4868 |
| gnu-rsa2048 | info | PASS (HTTP/1.1 200 OK, body 2862 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 2236 | 2139.605 | 2862 | 139 | - | 15 (0) | 1743.350 | 51588 | 16717 | 4500 |
| debian-rsa4096 | info | PASS (HTTP/1.1 200 OK, body 15864 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2342 | 2245.848 | 15864 | 148 | 2964.4 | 40 (0) | 1370.275 | 52256 | 16717 | 4468 |
| c64wiki-tls12 | info | PASS (HTTP/1.1 200 OK, body 64732 B, close-notify) | close-notify | 1.2 | 0xcca8 | X25519 | 2449 | 2092.670 | 64732 | 479 | 4465.6 | 121 (48) | 1907.188 | 49864 | 16717 | 4804 |
| craigslist-tls12 | info | PASS (HTTP/1.1 200 OK, body 2185 B = Content-Length, idle-timeout) | idle-timeout | 1.2 | 0xc02c | P-256 | 2231 | 2041.522 | 2185 | 20133 | - | 17 (0) | 1849.043 | 43644 | 16717 | 4852 |
| badssl-no-ems | info | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 161.097 | 38712 | 16717 | 6388 |
| c64wiki-require13 | info | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 135.327 | 37216 | 16717 | 6340 |
| bundle-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 5 (0) | 1676.459 | 47984 | 16717 | 4868 |
| local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1383 | 1378.571 | 262144 | 232 | 3028.9 | 434 (240) | 1174.269 | 42280 | 16717 | 4868 |
| local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1923 | 1916.852 | 262144 | 306 | 3417.1 | 436 (240) | 1634.056 | 39940 | 16717 | 4852 |
| local-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 511.339 | 37584 | 16717 | 4948 |
| local-selfsigned | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 180.759 | 37220 | 16717 | 4948 |
| local-selfsigned-bundle | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 238.279 | 37224 | 16717 | 4948 |
| local-expired | gate | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 654.280 | 37564 | 16717 | 4772 |
| local-no-ems | gate | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 274.050 | 37084 | 16717 | 6292 |
| local-require13-vs-tls12 | gate | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 360.182 | 37136 | 16717 | 6340 |
| local-pin | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1189 | 1184.772 | 262144 | 151 | 4478.0 | 434 (240) | 834.035 | 42292 | 16717 | 4948 |
| local-pin-wrong | gate | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 174.153 | 35776 | 16717 | 4948 |
| local-post-8k | gate | PASS (HTTP/1.0 200 OK, body 65 B = Content-Length, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 864 | 861.298 | 65 | 47 | - | 29 (0) | 703.250 | 42184 | 16717 | 4500 |
| local-close-established | gate | PASS (HTTP/1.1 200 OK, body 5 B = Content-Length, closed-by-client, server saw close_notify) | closed-by-client | 1.3 | 0x1302 | X25519 | 1154 | 1150.804 | 5 | 2 | - | 7 (0) | 986.784 | 39072 | 16717 | 4500 |
| local-no-time | gate | PASS (open 259 reason 2) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6596 |
| local-no-entropy | gate | PASS (open 259 reason 1) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6740 |
| local-sntp-clock | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1122 | 1119.917 | 262144 | 148 | 4655.5 | 434 (240) | 957.023 | 42248 | 16717 | 4868 |
| pair-local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1109 | 1105.627 | 262144 | 3047 | 3690.7 | 434 (240) | 913.280 | 83024 | 16717 | 4852 |
| pair-local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1301 | 1298.046 | 262144 | 1744 | 3842.7 | 436 (240) | 1134.196 | 83024 | 16717 | 4852 |

`SUMMARY net=qemu-openeth gate_pass=18 gate_fail=0 info_pass=9 info_fail=0`  
`VERDICT GREEN`  

### Readings
- **The gate's downloads are complete.**
  - Every 256 KiB download is 262,144 B with the expected SHA-256 and ends in close_notify. In the pair, the two downloads are interleaved step by step.
  - The 8 KiB POST reaches the server intact.
  - CLOSE on an ESTABLISHED session delivers a close_notify, which the server reports.
- **The BUNDLE name check fails closed.** en.wikipedia.org under the name `nomatch.invalid` gives exactly verify 0x04.
- **NO_MEM resends on the READ side are routine:** 240 of the 434-436 steps of every 256 KiB download (QEMU); 123-223 of 449-455 on the board. Handshake-side and write-side resends never happen here.
- **Stack:** the worst is **4,468 B free of 8,192** (www.debian.org, RSA-4096). That is the worst of the cases run, with the bridge's TLS-task call chain minus the UART send. On the board it is 4,272 B free, in the same case.
- **Heap** (QEMU, **no Wi-Fi driver**):
  - a successful single session uses 39.1-53.2 KB, and a failed handshake 35.5-48.0 KB;
  - two sessions interleaved use 80.3-83.0 KB;
  - the largest single allocation on the TLS task is 16,717 B in every case that reached the handshake.

  Only the per-session use carries over to the firmware. With Wi-Fi, on the board: 38.1-53.5 KB per session,
  33.7-49.3 KB for a failed handshake, 80.0-80.4 KB for two sessions, and a lowest free of 61.0 KB.

## On a classic ESP32 dev board (2026-10-02)
**Board:**
- ESP32-D0WD-V3, revision v3.1, 4 MB flash, at 160 MHz (the bridge sdkconfig's CPU frequency);
- open Wi-Fi;
- the servers and SNTP on a LAN host (OpenSSL 3.6.5, Python 3.13).

**Build:**
- the same engine (756b77fd) and container image as above;
- every one of the 8 images (clean and the seven mutants) has the same hash as its QEMU build, so the board ran the
  images QEMU ran;
- flashed with the README's command.

| Run | gate | info | VERDICT | required | per-case PASS/FAIL as in QEMU |
|---|---|---|---|---|---|
| clean, run 1 | 18/18 | 9/9 | GREEN | ok | yes |
| clean, run 2 | 18/18 | 9/9 | GREEN | ok | yes |
| mut1 | 12/18 | 5/9 | RED | ok | yes |
| mut2 | 3/18 | 1/9 | RED | ok | yes |
| m3b | 12/18 | 4/9 | RED | ok | yes |
| m4b | 17/18 (local-close-established: the server saw no close_notify) | 9/9 | RED | ok | yes |
| m5 | 12/18 (SHA-256 differs) | 9/9 | RED | ok | yes |
| m6b | 17/18 (bundle-wronghost: "got close-notify") | 9/9 | RED | ok | yes |
| m10 | 17/18 (local-post-8k: idle-timeout) | 9/9 | RED | ok | yes |

Every gate FAIL reason is the same as in QEMU. The only differences in the reason texts are byte counts from live
public hosts.

Board figures next to QEMU's. The board figures come from its two clean runs and the QEMU figures from the two clean
runs above.

| Figure | Board (Wi-Fi) | QEMU (open_eth) |
|---|---|---|
| Worst stack free (of 8,192 B) | **4,272 B** (www.debian.org, RSA-4096) | 4,468 B (the same case) |
| Heap free at start (internal 8-bit) | 166,564 B, largest block 110,592 | 191,632 B, largest block 110,592 |
| Lowest `heap_min` in any case | 60,968 B | 83,256 B |
| Per-session heap use, one session that completed | 38.1-53.5 KB | 39.1-53.2 KB |
| Per-session heap use, failed handshake | 33.7-49.3 KB | 35.5-48.0 KB |
| Two interleaved sessions | 79,964-80,352 B | 80,276-83,024 B |
| Largest single allocation on the TLS task | 16,717 B | 16,717 B |
| Longest single engine step | 2,545 ms (wikipedia handshake) | 2,925 ms (example) |
| READ-side NO_MEM resends per 256 KiB download | 123-223 (12 downloads) | 240 in every download |
| Steps per 256 KiB download | 449-455 | 434-436 |

Handshake time, from the engine's `handshake_us` (the two board runs agree to within 1%):

| Case | Board ms | QEMU ms |
|---|---|---|
| local-tls13 (X25519, ChaCha20) | 1,097 / 1,101 | 1,125 / 1,379 |
| local-tls12 (ECDHE, ChaCha20) | 1,250 / 1,252 | 1,382 / 1,917 |
| local-pin | 722 / 722 | 769 / 1,185 |
| gnu-rsa2048 | 620 / 623 | 2,337 / 2,140 |
| debian-rsa4096 | 680 / 680 | 2,740 / 2,246 |
| wikipedia (ECDSA) | 2,874 / 2,868 | 2,966 / 2,749 |
| example (ECDSA) | 2,531 / 2,532 | 3,491 / 2,938 |
| craigslist-tls12 (P-256 ECDSA) | 2,330 / 2,336 | 3,806 / 2,042 |

Board readings:
- **Bulk crypto.** The engine's time for a 256 KiB download is 256-275 ms, about 930-1,000 KiB/s at 160 MHz
  (ChaCha20-Poly1305). QEMU took 55-85 ms.
- **Wall-clock transfer is network-bound.** A 256 KiB download takes 1.5-4.4 s of wall time on the board, of which
  about 0.26 s is the engine.
  - Inferred, not measured: the limit is this app's own lwIP TCP window over Wi-Fi (`CONFIG_LWIP_TCP_WND_DEFAULT`
    5744 from the bridge sdkconfig).
  - In the firmware the Ultimate owns the TCP connection, so these wall times are this app's, not the product's.
- **Handshakes.**
  - RSA-server handshakes take 0.62-0.68 s on the board, against 2.1-2.7 s in QEMU. Inferred, not profiled: the
    ESP32's RSA/MPI accelerator in silicon, against QEMU's model.
  - ECDSA-server handshakes take 1.8-2.9 s.
- **Task watchdog.** The longest single step is 2.54 s, against a 5 s task watchdog
  (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`). No watchdog line appeared in any board console.
- **Heap with Wi-Fi.** The Wi-Fi driver takes about 25 KB. With two sessions at their peak, about 61 KB of internal
  heap stays free.
- **Entropy gate.**
  - The board took the Wi-Fi path (`net=wifi`), and every OPEN that was not stubbed succeeded.
  - `local-no-entropy` was still refused with NO_ENTROPY.
  - That `entropy_source()` reported RADIO is inferred from the code path (`esp_wifi_start()` returned before the
    first OPEN). The app does not print the entropy source.

Clean run 1 on the board:

| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B | transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B | stack free B (of 8192) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wikipedia | info | PASS (HTTP/1.1 200 OK, body 751108 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2982 | 2873.990 | 751108 | 6762 | 958.1 | 1286 (485) | 2544.684 | 49228 | 16717 | 4676 |
| github | info | PASS (HTTP/1.1 200 OK, body 16739 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2016 | 1820.637 | 16739 | 300 | 476.8 | 34 (0) | 1482.714 | 45808 | 16717 | 4676 |
| example | info | PASS (HTTP/1.1 200 OK, body 577 B, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 2624 | 2531.076 | 577 | 83 | - | 9 (0) | 2203.405 | 46688 | 16717 | 4676 |
| gnu-rsa2048 | info | PASS (HTTP/1.1 200 OK, body 2862 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 700 | 620.240 | 2862 | 280 | - | 14 (0) | 290.607 | 51504 | 16717 | 4500 |
| debian-rsa4096 | info | PASS (HTTP/1.1 200 OK, body 15864 B = Content-Length, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 773 | 680.469 | 15864 | 401 | 629.2 | 39 (0) | 248.578 | 53464 | 16717 | 4276 |
| c64wiki-tls12 | info | PASS (HTTP/1.1 200 OK, body 64732 B, close-notify) | close-notify | 1.2 | 0xcca8 | X25519 | 1314 | 757.887 | 64732 | 3870 | 998.6 | 125 (0) | 592.221 | 50752 | 16717 | 4804 |
| craigslist-tls12 | info | PASS (HTTP/1.1 200 OK, body 2185 B = Content-Length, idle-timeout) | idle-timeout | 1.2 | 0xc02c | P-256 | 2439 | 2330.366 | 2185 | 20094 | - | 15 (0) | 2163.674 | 43004 | 16717 | 4660 |
| badssl-no-ems | info | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 160.924 | 36764 | 16717 | 6340 |
| c64wiki-require13 | info | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 160.889 | 34964 | 16717 | 6340 |
| bundle-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 5 (0) | 2153.944 | 47572 | 16717 | 4868 |
| local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1419 | 1096.679 | 262144 | 3155 | 950.1 | 450 (208) | 935.716 | 42280 | 16717 | 4868 |
| local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1310 | 1250.242 | 262144 | 1926 | 1001.8 | 455 (123) | 1085.833 | 39720 | 16717 | 4900 |
| local-wronghost | gate | PASS (fail_reason 4 verify 0x00000004 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 549.802 | 37452 | 16717 | 4868 |
| local-selfsigned | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 172.511 | 35140 | 16717 | 4948 |
| local-selfsigned-bundle | gate | PASS (fail_reason 4 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 173.988 | 36036 | 16717 | 4948 |
| local-expired | gate | PASS (fail_reason 4 verify 0x00000001 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 552.619 | 37476 | 16717 | 4868 |
| local-no-ems | gate | PASS (fail_reason 3 verify 0x00000000 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 161.106 | 33660 | 16717 | 6340 |
| local-require13-vs-tls12 | gate | PASS (fail_reason 0 verify 0x00000000 alert_in 70) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 161.041 | 35012 | 16717 | 6340 |
| local-pin | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 766 | 721.612 | 262144 | 1939 | 964.7 | 449 (192) | 560.816 | 41188 | 16717 | 4948 |
| local-pin-wrong | gate | PASS (fail_reason 5 verify 0x00000008 alert_in 255) | tls-failed | - | 0x0000 | - | - | - | 0 | - | - | 3 (0) | 172.313 | 35324 | 16717 | 4948 |
| local-post-8k | gate | PASS (HTTP/1.0 200 OK, body 65 B = Content-Length, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1302 | X25519 | 1150 | 1108.301 | 65 | 17 | - | 29 (0) | 947.042 | 48372 | 16717 | 4500 |
| local-close-established | gate | PASS (HTTP/1.1 200 OK, body 5 B = Content-Length, closed-by-client, server saw close_notify) | closed-by-client | 1.3 | 0x1302 | X25519 | 1122 | 1106.290 | 5 | 148 | - | 9 (0) | 945.893 | 38680 | 16717 | 4500 |
| local-no-time | gate | PASS (open 259 reason 2) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6740 |
| local-no-entropy | gate | PASS (open 259 reason 1) | none (open-refused) | - | 0x0000 | - | - | - | 0 | - | - | 0 (0) | 0.000 | - | 0 | 6740 |
| local-sntp-clock | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1140 | 1097.485 | 262144 | 1947 | 961.6 | 449 (207) | 937.127 | 41588 | 16717 | 4868 |
| pair-local-tls13 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.3 | 0x1303 | X25519 | 1118 | 1098.784 | 262144 | 3669 | 988.0 | 450 (165) | 938.255 | 79964 | 16717 | 4852 |
| pair-local-tls12 | gate | PASS (HTTP/1.0 200 ok, body 262144 B, SHA-256 match, close-notify) | close-notify | 1.2 | 0xcca9 | X25519 | 1303 | 1257.611 | 262144 | 2295 | 1003.5 | 450 (208) | 1092.778 | 79964 | 16717 | 4852 |

## Earlier revisions (superseded)
- **The first version's oracle** did not check byte counts, hashes or how a session ended. An engine that dropped
  99.5% of the data also passed.
- **The second version's gate** missed four mutants. Three of them are now required reds (m4b, m6b, m10), and the
  fourth (m8c) is a disclosed gap. That version also gave a single-session heap figure of 34.1 KB, which was an
  artifact: the baseline was taken before the CA upload. Its earlier timing note said "up to 4x"; the swing was
  4.4x.
- All earlier runs used an engine snapshot whose code is identical to `esp32-tls-offload` and only its comments
  differ. They are not cited here.
