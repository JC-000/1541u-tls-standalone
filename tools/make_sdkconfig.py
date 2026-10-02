#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""make_sdkconfig.py: writes ./sdkconfig as the firmware bridge's own sdkconfig (engine/software/wifi/raw_u64/sdkconfig
at ENGINE_SHA) with only the changes in DELTA. Run `idf.py reconfigure` after it; tools/diff_sdkconfig.sh then shows
every line that differs from the firmware's, and that no CONFIG_MBEDTLS_ line differs except the bundle path."""
import pathlib, re

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "engine/software/wifi/raw_u64/sdkconfig"
DELTA = {
    # QEMU needs a 2/4/8/16 MB image; the app has Wi-Fi, open_eth and SNTP besides the engine, so the 1.5 MB app
    # partition. A classic ESP32 dev board has 4 MB.
    "CONFIG_ESPTOOLPY_FLASHSIZE_2MB": None,
    "CONFIG_ESPTOOLPY_FLASHSIZE_4MB": "y",
    "CONFIG_ESPTOOLPY_FLASHSIZE": '"4MB"',
    "CONFIG_PARTITION_TABLE_SINGLE_APP": None,
    "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE": "y",
    # QEMU's NIC
    "CONFIG_ETH_USE_OPENETH": "y",
    # The largest single allocation on the TLS task (engine_task.c)
    "CONFIG_HEAP_USE_HOOKS": "y",
    # The same image from the same sources: paths mapped, no build time in the app header; __DATE__/__TIME__ come
    # from SOURCE_DATE_EPOCH (tools/run_qemu.sh sets it to the engine commit's time)
    "CONFIG_APP_REPRODUCIBLE_BUILD": "y",
    "CONFIG_APP_COMPILE_TIME_DATE": None,
    # The same store, reached from this project
    "CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH": '"engine/software/wifi/ca_store/cacert.pem"',
}

out = []
seen = set()
for line in FIRMWARE.read_text().splitlines():
    m = re.match(r"^(?:# )?(CONFIG_[A-Z0-9_]+)(?:=| is not set)", line)
    if m and m.group(1) in DELTA:
        key = m.group(1)
        seen.add(key)
        value = DELTA[key]
        out.append("%s=%s" % (key, value) if value else "# %s is not set" % key)
    else:
        out.append(line)
missing = set(DELTA) - seen
for key in sorted(missing):
    if DELTA[key]:
        out.append("%s=%s" % (key, DELTA[key]))
(ROOT / "sdkconfig").write_text("\n".join(out) + "\n")
print("sdkconfig written; not found in the firmware's file (appended):", sorted(missing))
