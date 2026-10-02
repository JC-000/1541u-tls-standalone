# 1541u-tls-standalone

The 1541 Ultimate's ESP32 TLS engine, run on its own. The app is built from FreeRTOS, lwIP and mbedTLS (stock
ESP-IDF 6.0.3) plus the engine, with no Ultimate code linked. It makes real outgoing HTTPS requests. It checks
every byte that comes back. It measures handshake time, throughput, heap and stack.

## Why
On GideonZ/1541ultimate#936, Gideon wrote
([comment](https://github.com/GideonZ/1541ultimate/pull/936#issuecomment-5876427638)):

> Let's not integrate this at all, until it is completely stable in a test project that doesn't have anything
> ultimate related. Just FreeRTOS, lwIP and the TLS additions, combined with an outgoing request and possibly an
> incoming server. This helps tremendously in isolating issues, do performance measurements and all. You won't
> need any communication with the ESP32 either, and it can be tested and benchmarked on all platforms.

This app **addresses the isolation part of that condition** for our ESP32 TLS engine:
- the engine runs with no Ultimate code linked and with no UART link to an Ultimate;
- the outgoing requests are real;
- the measurements are taken.

Gideon's comment can also be read as asking for TLS on the Ultimate's own CPU, with no ESP32 at all. This app does
not answer that reading, and we are asking him which one he meant. (He also wrote that he "would personally opt for
the mbedtls library and not try to create anything ourselves"
([comment](https://github.com/GideonZ/1541ultimate/pull/936#issuecomment-5890938239)); the engine uses stock
mbedTLS.)

What it does not do:
- **It is an ESP32 app.** Our design puts TLS on the Ultimate's ESP32 co-processor, so the engine needs an ESP-IDF
  target. It is not "tested and benchmarked on all platforms" in the sense of a host or U2-family build.
- **No incoming server.** The engine is a TLS client only.

It is evidence for the ESP32 TLS engine PR. It is not meant to be merged.

## What is compiled
| Part | Source |
|---|---|
| `tls_engine.c`, `tls_engine.h` | **unmodified**, from the firmware commit `ENGINE_SHA` in `engine.env`, fetched into `./engine` |
| `rpc_calls.h`, `wifi_modem.h`, `sntp.h`, `cmd_buffer.h` | the same commit. These are bridge headers that the engine's own `#include`s name. They are declarations only; none of their `.c` files is linked |
| `software/wifi/raw_u64/sdkconfig` | the same commit: the bridge's configuration is the base of this app's (see Configuration) |
| Mozilla root store `software/wifi/ca_store/cacert.pem` | the same commit, checked by its own `tools/ca_store/ca_store.py` |
| mbedTLS 4.1.1, lwIP, FreeRTOS | stock ESP-IDF v6.0.3, no patches |
| `main/engine_task.c` | the "TLS" task (8,192 B, core 1, `tskIDLE_PRIORITY + 1`) and the bridge's six `cmd_tls_*` handlers, transcribed from `rpc_dispatch.c:375-550` without the UART send |
| `main/app.c` | the Ultimate's part: the TCP socket, the RPC requests, the reply checks, and the step pump |
| `main/stubs.c` | `entropy_source()` and `sntp_has_time()`, the two engine dependencies outside mbedTLS/IDF |
| `main/net.c`, `main/http.c` | Wi-Fi station (dev board) or QEMU open_eth, then SNTP; the HTTP reply's length and SHA-256 |

So, plainly: **no Ultimate code is linked**, but the engine still includes four bridge headers, and the app
starts from the bridge's sdkconfig.

### Which engine, and re-pointing it
`engine.env` holds `ENGINE_URL` and `ENGINE_SHA`.
- `tools/fetch_engine.sh` fetches that commit into `./engine`. `ENGINE_URL` is `[SOURCE BRANCH LINK]` until the
  source is published; meanwhile `ENGINE_URL_ENV=<clone or url>` overrides it.
- `tools/check_engine.cmake` refuses a checkout at another commit, or one with any tracked file changed. It runs
  at configure and again at every build, before `main` is compiled, so an incremental build cannot slip in an
  edited engine.
- The configure log prints the sha256 of `tls_engine.c` and `tls_engine.h`.

**The results in RESULTS.md are for the engine bytes at the `ENGINE_SHA` they name.** Any other engine commit,
including the one in the engine PR, must be run again before it is cited.

To test another commit, set `ENGINE_SHA`, fetch and build. Two things follow that commit by themselves:
- the sdkconfig, regenerated from that commit's own file by `tools/make_sdkconfig.py` at every run;
- `tools/verify_transcriptions.py`, run at configure and at every build. It compares the app's hand copies of
  firmware code with that commit's sources (comments and layout ignored):
  - the bridge's `cmd_tls_*` handlers (`rpc_dispatch.c`), copied into `main/engine_task.c`;
  - the Ultimate's `wifi_tls_take_step()` (`wifi_tls.cc`), copied into `take_step()` in `main/app.c`;
  - the Ultimate's step pump (`network_target.cc`), ported into `main/app.c`.

  The firmware regions must equal the references in `tools/transcriptions/*.ref` that the copies were made from,
  and each copy must differ from its reference by exactly the declared delta in `tools/transcriptions/*.delta`.
  Any other difference fails the build. A commit without those files (for example one that carries the engine but
  not yet the Ultimate side) fails too. Updating the copies is a reviewed change (`--update`), never automatic.

### How the engine is driven
In the firmware, the Ultimate owns the TCP connection and moves the ciphertext to the ESP32 in UART RPCs
(`CMD_TLS_OPEN/STEP/CLOSE/STATS/SET_CA`). Here a FreeRTOS queue replaces the UART, and everything else keeps the
firmware's shape:

- **The request and reply buffer is the bridge's.** Every request is a `command_buf_t` laid out per
  `rpc_calls.h`. The handler writes its reply over it, exactly as `rpc_dispatch.c` does. In particular, STEP's
  output (`resp->data`, offset 20) overlays its input (`req->data`, offset 12): see `rpc_dispatch.c:407-416`.
- **Each STEP reply passes the Ultimate's checks.** These are ported from `wifi_tls_take_step()`
  (`wifi_tls.cc:176-241`):
  - exact size;
  - a state is present;
  - `fail_reason` only with FAILED;
  - ct/pt within the Ultimate's 1,536/1,024 B buffers;
  - no plaintext from a NO_READ step, and none outside ESTABLISHED/CLOSED;
  - NO_MEM is "again" while live, and "closed" once CLOSED.

  A violation fails the case as `contract-violation`.
- **The pump follows `network_target.cc`:**
  - the handshake (`:1336`): the first step has no input; the input is resent on NO_MEM; all ciphertext goes out
    before the next step; STATS after ESTABLISHED;
  - WRITE (`:2070`): NO_READ steps;
  - READ (`:1921`): an eager top-up of up to 1,536 B per step; the end is close_notify, or TCP FIN followed by two
    quiet steps;
  - CLOSE: the close_notify is sent (the `local-close-established` case checks that the server receives it).

The line numbers are at f2e46946.

What it does not exercise:
- the UART transport, the RPC framing and its timeouts;
- the TLS queue's busy reply (`cmd_tls_busy()`);
- the Ultimate's reset and cancel paths.

On the TLS task the call chain is the bridge's own (`tls_task` → `tls_serve` → `cmd_tls_*` → engine). Only the UART
send, which runs after the engine returns, is missing.

### Configuration
`tools/make_sdkconfig.py` writes `sdkconfig` as the engine commit's own `software/wifi/raw_u64/sdkconfig` with
these changes. `tools/diff_sdkconfig.sh` prints the whole diff (it is in every run's evidence).

| Option | Why |
|---|---|
| `ESPTOOLPY_FLASHSIZE` 2MB → 4MB, `PARTITION_TABLE_SINGLE_APP_LARGE` | QEMU needs a 2/4/8/16 MB image; the app also carries open_eth and SNTP; a dev board has 4 MB |
| `ETH_USE_OPENETH=y` | QEMU's NIC |
| `HEAP_USE_HOOKS=y` | to measure the largest single allocation on the TLS task |
| `MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH` | the same `cacert.pem`, reached from this project |

Two more differences come from regeneration, not from choice:
- the bridge's `EXAMPLE_*` options disappear, because its `Kconfig.projbuild` is not part of this app;
- the bundle path is the only `CONFIG_MBEDTLS_` difference.

## The test oracle
Each case prints a `RESULT` line and a `CASE <name> gate|info PASS|FAIL (why)` line. The run ends with
`SUMMARY` and **`VERDICT GREEN|RED`**.

- **Gate cases** are the local servers (deterministic), plus one case that needs the network:
  `bundle-wronghost`. The BUNDLE trust mode has its own branch in the engine's verify callback, and only a
  Mozilla-trusted server can reach it, so that case connects to en.wikipedia.org with the wrong name
  (`nomatch.invalid`) and requires the name check to fail. Without the network that one gate case fails, and so
  does the VERDICT. VERDICT is GREEN only if every gate case passes.
- **Info cases** are public hosts. They are informational, because a host can change its certificate, its TLS
  versions or its replies without the engine changing.
- **A success case passes only if all of these hold:**
  - the session reached ESTABLISHED with the expected TLS version and clock source;
  - the HTTP status is 200;
  - the session **ended with close_notify**, unless that case explicitly accepts another end, and any other end
    also needs a matching Content-Length;
  - the body length equals `Content-Length` whenever the server sends one;
  - for the local 256 KiB download (`tools/mkblob.py`, deterministic), the body is **exactly 262,144 B with the
    expected SHA-256** (`certs/blob.h`);
  - public bodies are checked by length only, not by content.
- **A failure case passes only on the expected failure:** the fail reason, the verify-flag bits, and the alert
  where one is named (alert 70 for REQUIRE_TLS13 against TLS 1.2).
- **Ends are classified explicitly:** close-notify, tcp-fin-no-notify, rx-error, idle-timeout, read-timeout,
  tls-failed, error. Each RESULT line also counts the NO_MEM resend steps (`again_steps`).

### Red/green
`tools/redgreen.sh <dir>` runs the clean engine, which must be **GREEN**. It then runs every mutant in
`tools/mutate.py`'s `REQUIRED_RED`, each of which must be **RED**:

| Mutant | Known-bad change | Caught by |
|---|---|---|
| mut1 | drops all plaintext after 4 KiB | body length/SHA, the end |
| mut2 | writes the step output before consuming the input | the bridge's overlaid buffer: handshakes fail |
| m3b | drops the last record before close_notify | body length/SHA |
| m4b | `tls_close()` sends no close_notify | `local-close-established`: the server reports no close_notify |
| m5 | flips one bit at the 128 KiB mark | SHA-256 |
| m6b | accepts a wrong host name under BUNDLE trust | `bundle-wronghost` |
| m10 | a write keeps only its first 512 B | `local-post-8k`: the server's SHA-256 of the 8 KiB it received |

`m8c` (the handshake does not run on a step whose input was refused) is defined, but **no case catches it**: no
handshake step is ever refused here. See Limits.

Each mutant is applied by `tools/mutate.py` to a **copy** of the engine. The engine check allows exactly that one
changed file and labels the build and the console `MUTANT`.

## Build and run in QEMU (no hardware)
Requires Docker and git.
- The image is about 15.6 GB.
- The repo must sit on a path Docker can bind-mount (Colima, for example, mounts only `$HOME` by default).
- On Linux the container runs as root and writes `build/` and `sdkconfig` into the checkout as root.

```
ENGINE_URL_ENV=<clone or url> tools/fetch_engine.sh     # ./engine at ENGINE_SHA
tools/docker.sh <logdir> [mut1|mut2]                  # one run
tools/redgreen.sh <logdir>                            # clean GREEN + both mutants RED
```

`docker.sh` uses `espressif/idf` v6.0.3, pinned per architecture:
- arm64: `sha256:da1f22c157dcb4f684f822ff37be17dc0abef82160d889bfd7fd23cc1299f0fa`
- amd64: `sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e`

Both come from the index `sha256:54278f2c…`. The two are different toolchain and QEMU builds; our runs used
arm64.

Only the repo (as `/work`) and the log directory (as `/out`) are mounted. Each run then:
- writes `sdkconfig` and builds;
- starts the local servers (`tools/servers.sh`, including an SNTP server, so a run does not depend on public NTP);
- runs `qemu-system-xtensa -M esp32` with user-mode networking (QEMU's host is 10.0.2.2) until `STANDALONE_DONE`;
- writes `<logdir>/results.md`.

Without internet the info cases and `bundle-wronghost` fail. The local cases still run, because SNTP is local.

**Reproducible image.** `CONFIG_APP_REPRODUCIBLE_BUILD` is set, and `run_qemu.sh` sets `SOURCE_DATE_EPOCH` to the
engine commit's time. So `__DATE__` (also in the engine's clock floor, `tls_build_time()`) is that commit's date,
not the build day. RESULTS.md says whether two builds gave the same `tls_standalone.bin`.

**QEMU timings are not hardware timings.** QEMU does not model the ESP32's clock, and the crypto accelerators are
QEMU models. Treat every time as a comparison between cases only. Between our runs, the same case varied by up to
4.4x with host load.

**The QEMU heap has no Wi-Fi driver.** The firmware runs Wi-Fi and its buffers, so its free heap and its
fragmentation differ. Of the heap figures, only the per-session use (peak minus baseline) carries over, and even
that includes lwIP's open_eth buffers. The stack figure is the worst of the cases run, without silicon interrupt
nesting.

## Limits (what the gate does not catch)
- **Refused handshake steps** (`ESP_ERR_NO_MEM` while handshaking) never happen here, so a bug there stays GREEN (`m8c`).
- **Write-side NO_MEM resends.** Only the READ side's resends run (240 per 256 KiB download).
- **Name and expiry checks.**
  - Expiry is checked under CUSTOM trust only, the name under CUSTOM and BUNDLE.
  - PIN has only its wrong-key case.
- **`tls_set_ca` refusals**, a CA tag mismatch (`NO_CUSTOM_CA`), and the engine's own VERSION_REFUSED/DOWNGRADE
  classification. In the REQUIRE_TLS13 cases the server aborts first, with alert 70.
- **Public-host bodies** are checked by length, not content.
- **Robustness:** HelloRetryRequest, P-384/P-521 key exchange, soak, repetition and fault injection (resets,
  truncated records, slow peers).
- **The firmware context:** the UART, the queue's busy path, and anything on hardware.

## Run on a classic ESP32 dev board (not done yet)
The same image runs on a classic ESP32 (ESP32-D0WD, 4 MB flash; the U64's module is a classic ESP32) over Wi-Fi.
The app reads its settings from NVS (namespace `standalone`):
- `ssid` and `pass`: with no credentials it takes the QEMU path;
- `local`: the LAN host running the servers;
- `ntp` (optional): the default is pool.ntp.org.

On a board the entropy gate is real: `entropy_source()` reports RADIO only after `esp_wifi_start()` succeeds.

1. On a LAN host with OpenSSL 3 and python3, as root for UDP 123 (or set `NTP_PORT` and NVS `ntp`), run
   `tools/servers.sh start /tmp/srv`.
2. Write the NVS image:
   ```
   cat > nvs.csv <<EOF
   key,type,encoding,value
   standalone,namespace,,
   ssid,data,string,<SSID>
   pass,data,string,<password>
   local,data,string,<LAN host IP>
   ntp,data,string,<LAN host IP or pool.ntp.org>
   EOF
   python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate nvs.csv nvs.bin 0x6000
   ```
3. Flash the image built above and the NVS image (`nvs` is at 0x9000 in the single-app-large table; check it in
   `build/partition_table`):
   ```
   python -m esptool --chip esp32 -p <port> -b 460800 write-flash --flash-mode dio --flash-size 4MB --flash-freq 40m \
       0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin \
       0x10000 build/tls_standalone.bin 0x9000 nvs.bin
   ```
4. Capture the console at 115200 baud until `STANDALONE_DONE`, then run
   `grep -E '^(RESULT|CASE|SUMMARY|VERDICT)' log > results.txt; tools/results.py results.txt`.
5. Stop the servers: `tools/servers.sh stop /tmp/srv`.

## Cases
- **Info (public hosts, Mozilla bundle):**
  - TLS 1.3: en.wikipedia.org `/wiki/Commodore_64` (about 750 KB), github.com, example.com (all ECDSA); www.gnu.org (RSA-2048); www.debian.org (RSA-4096);
  - TLS 1.2 with EMS: www.c64-wiki.com (ECDHE-RSA) and www.craigslist.org (ECDHE-ECDSA). Craigslist needs HTTP/1.1 and leaves the connection open, so it accepts an idle end, and only together with a Content-Length match;
  - tls-v1-2.badssl.com:1012 (TLS 1.2 without EMS: refused);
  - c64-wiki with REQUIRE_TLS13 (alert 70).
- **Gate (local OpenSSL servers, private CA uploaded as trust CUSTOM):**
  - TLS 1.3 and TLS 1.2 256 KiB downloads (SHA-256 checked);
  - an 8 KiB POST, written in 896 B WRITEs (whole 32 B blocks up to `WRITE_CHUNK`), whose SHA-256 the server sends back (`tools/app_server.py`);
  - CLOSE while ESTABLISHED: a kept-open connection is closed by the client after its body, and the server must
    report that a close_notify arrived;
  - BUNDLE trust with the wrong host name (en.wikipedia.org, network needed; see the oracle);
  - wrong host name, self-signed (CUSTOM and BUNDLE), expired;
  - TLS 1.2 without EMS, and REQUIRE_TLS13 against TLS 1.2;
  - PIN, right key (download checked) and wrong key;
  - OPEN without a clock (NO_TIME);
  - OPEN without entropy (stub flipped: NO_ENTROPY);
  - the engine's own-SNTP clock branch (stub flipped: time source SNTP, download checked);
  - **two sessions at once** (the engine's maximum): the handshakes run one after the other, then both 256 KiB downloads run **interleaved step by step**, both checked.

`certs/` holds a test PKI generated by `tools/mkcerts.sh` (OpenSSL ≥ 3.4). The keys are committed and protect
nothing. **Never install `certs/ca.pem` in a trust store.**

## Results
See `RESULTS.md`, which records which run, which engine and which image produced them.
