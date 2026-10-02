#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# diff_sdkconfig.sh: every option that differs between this app's sdkconfig and the firmware bridge's (the engine checkout, engine.env ENGINE_SHA)
cd "$(dirname "$0")/.."
norm() { grep -E '^(CONFIG_|# CONFIG_.* is not set)' "$1" | sort; }
diff <(norm engine/software/wifi/raw_u64/sdkconfig) <(norm sdkconfig)
echo "--- CONFIG_MBEDTLS_ lines that differ:"
diff <(norm engine/software/wifi/raw_u64/sdkconfig | grep MBEDTLS_) <(norm sdkconfig | grep MBEDTLS_)
exit 0
