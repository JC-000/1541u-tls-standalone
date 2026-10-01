# 1541u-tls-standalone

The 1541 Ultimate's ESP32 TLS engine on its own: FreeRTOS, lwIP and mbedTLS from ESP-IDF 6.0.3, the engine,
and nothing of the Ultimate. It makes real outgoing HTTPS requests and measures handshake time, throughput,
heap and stack.

It answers Gideon's condition on GideonZ/1541ultimate#936
([comment](https://github.com/GideonZ/1541ultimate/pull/936#issuecomment-5876427638)): "completely stable in a
test project that doesn't have anything ultimate related. Just FreeRTOS, lwIP and the TLS additions, combined
with an outgoing request". It is evidence for the ESP32 TLS engine PR. It is not meant to be merged.

## What is compiled

| Part | Source |
|---|---|
| `tls_engine.c`, `tls_engine.h` | **unmodified**, from GideonZ/1541ultimate fork commit `f2e46946` (branch esp-tls/m3), through the symlink `engine` |
| headers the engine includes (`rpc_calls.h`, `wifi_modem.h`, `sntp.h`, `cmd_buffer.h`) | the same commit. Declarations only; none of their `.c` files is linked |
| Mozilla root store (`software/wifi/ca_store/cacert.pem`) | the same commit, checked by its own `tools/ca_store/ca_store.py` |
| mbedTLS 4.1.1, lwIP, FreeRTOS | stock ESP-IDF v6.0.3 (`espressif/idf:v6.0.3@sha256:54278f2c…`), no patches |
| `main/stubs.c` | `entropy_source()` and `sntp_has_time()`, the two engine dependencies outside mbedTLS/IDF |
| `main/engine_task.c` | the "TLS" task: 8,192 B stack, core 1, `tskIDLE_PRIORITY + 1`, as the bridge's `rpc_dispatch.c` |
| `main/app.c` | the Ultimate's part: DNS, the TCP socket, and the step pump of `network_target.cc` |
| `main/net.c` | Wi-Fi station (dev board) or QEMU's open_eth, then SNTP |

The configure step fails unless every file taken from `engine/` has the SHA-256 pinned in `CMakeLists.txt`:

```
tls_engine.c  5ee04a59562245dd79bf5e7f7ee5cf4e30abb76bbfc4dc2d5e9b56d680da5361
tls_engine.h  efabf8a2450c5f4eb7ca7a2549ef71a75e58aa191a6c41b536ea648ef333b109
```

### How the engine is driven
The engine never touches a socket. In the firmware, the Ultimate owns the TCP connection and moves the
ciphertext over UART RPCs (`CMD_TLS_OPEN/STEP/CLOSE/STATS/SET_CA`) to the ESP32. Here `app.c` owns an lwIP
socket and calls the same engine functions on the same kind of task. A FreeRTOS queue replaces the UART. The
pump follows `software/io/network/network_target.cc` at f2e46946:

- the handshake: the first step has no input; the input is resent on `ESP_ERR_NO_MEM`; all ciphertext goes out before the next step;
- STATS right after ESTABLISHED;
- WRITE as `TLS_STEP_NO_READ` steps;
- READ with an eager top-up of up to 1,536 B of ciphertext per step;
- the end of a stream is close_notify or TCP FIN followed by two quiet steps;
- CLOSE sends the close_notify.

What it does not exercise: the UART transport, the RPC framing and its timeouts, and the Ultimate's
reset/cancel paths.

### Configuration
`sdkconfig` is the bridge's own `software/wifi/raw_u64/sdkconfig` at f2e46946 with these changes and no others
(`tools/make_sdkconfig.py`; `tools/diff_sdkconfig.sh` prints the diff):

| Option | Why |
|---|---|
| `ESPTOOLPY_FLASHSIZE` 2MB → 4MB, `PARTITION_TABLE_SINGLE_APP_LARGE` | QEMU needs a 2/4/8/16 MB image; this image also carries open_eth and SNTP; a dev board has 4 MB |
| `ETH_USE_OPENETH=y` | QEMU's NIC |
| `HEAP_USE_HOOKS=y` | to measure the largest single allocation on the TLS task |
| `MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH` | the same `cacert.pem`, reached from this project |

No other `CONFIG_MBEDTLS_` option differs.

## Build and run in QEMU (no hardware)

Requires Docker. `$HOME/Documents` is mounted read-only at the same path. `engine` must point at a checkout of
f2e46946 (here: `git worktree add --detach ../1541u-wt/standalone-engine f2e46946`).

```
tools/docker.sh <logdir>
```

This builds the image, starts the local test servers (`tools/servers.sh`), runs the image in
`qemu-system-xtensa -M esp32` with user-mode networking (QEMU's host is 10.0.2.2), stops at `STANDALONE_DONE`,
and writes `<logdir>/results.md`.

**QEMU timings are not hardware timings.** QEMU does not emulate the ESP32's clock: code runs as fast as the
host's emulation of Xtensa allows, and the crypto accelerators are QEMU models. Heap and stack figures do
transfer to hardware (same code, same allocator), apart from interrupt nesting on the stack.

## Run on a classic ESP32 dev board (not done yet)

The same image runs on a classic ESP32 (ESP32-D0WD, 4 MB flash; the U64's module is a classic ESP32) over
Wi-Fi. The app reads the Wi-Fi credentials from NVS; with no credentials it takes the QEMU path.

1. Start the local servers on a LAN host with OpenSSL 3 and python3: `tools/servers.sh start /tmp/srv`.
2. Write an NVS image with the credentials and that host's address:
   ```
   cat > nvs.csv <<EOF
   key,type,encoding,value
   standalone,namespace,,
   ssid,data,string,<SSID>
   pass,data,string,<password>
   local,data,string,<LAN host IP>
   EOF
   python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate nvs.csv nvs.bin 0x6000
   ```
3. Flash the image built above (`build/`) and the NVS image (`nvs` is at 0x9000 in the single-app-large table):
   ```
   python -m esptool --chip esp32 -p <port> -b 460800 write-flash --flash-mode dio --flash-size 4MB --flash-freq 40m \
       0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin \
       0x10000 build/tls_standalone.bin 0x9000 nvs.bin
   ```
4. Capture the console at 115200 baud (`idf.py -p <port> monitor`, or any serial terminal) until `STANDALONE_DONE`,
   save it, then run `grep -E '^(RESULT|CASE|SUMMARY)' log > results.txt; tools/results.py results.txt`.
5. Stop the servers: `tools/servers.sh stop /tmp/srv`.

On a board the entropy gate is real: `entropy_source()` reports RADIO only after `esp_wifi_start()` succeeds,
as in the bridge. In QEMU there is no radio, so the open_eth path reports RADIO as a declared stub.

## Cases
Each case prints a `RESULT` line and a `CASE … PASS|FAIL` line that checks the result against the case's
expectation.

- Public hosts (Mozilla bundle):
  - en.wikipedia.org `/wiki/Commodore_64` (about 750 KB, the throughput case), github.com, example.com: ECDSA, TLS 1.3;
  - www.gnu.org: RSA-2048, TLS 1.3;
  - www.debian.org: RSA-4096, TLS 1.3;
  - www.c64-wiki.com (ECDHE-RSA) and www.craigslist.org (ECDHE-ECDSA): TLS 1.2 with EMS;
  - tls-v1-2.badssl.com:1012: TLS 1.2 without EMS, which the engine refuses;
  - www.c64-wiki.com with REQUIRE_TLS13, which must fail.
- Local OpenSSL servers (private CA uploaded as trust CUSTOM):
  - TLS 1.3 and TLS 1.2, each with a 256 KiB download;
  - wrong host name, self-signed (CUSTOM and BUNDLE), expired;
  - TLS 1.2 without EMS, and REQUIRE_TLS13 against a TLS 1.2 server;
  - PIN (right and wrong key);
  - OPEN without a clock (refused NO_TIME).
- Two sessions at once (the engine's maximum): a local session established and idle while the Wikipedia download runs.

The engine is a client only: it has no server mode, so the "incoming server" Gideon mentioned is not part of
this app.

## Results
See `RESULTS.md`, which records which run and which image produced them.
