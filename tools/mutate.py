#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""mutate.py <mutant> <tls_engine.c>: applies one known-bad change to a COPY of the engine (tools/run_qemu.sh MUTANT=).
Each entry is a list of (anchor, replacement); every anchor must occur exactly once. tools/redgreen.sh requires every
mutant in REQUIRED_RED to turn the run RED. m8c is listed but not required: no case reaches a refused handshake step
(see README, limits)."""
import sys

REQUIRED_RED = ["mut1", "mut2", "m3b", "m4b", "m5", "m6b", "m10"]

MUTANTS = {
    "mut1": [("    *pt_out_len = pt_len;\n",
              "    *pt_out_len = (s->stats.pt_out_bytes > 4096) ? 0 : pt_len; // MUT1\n")],
    "mut2": [("    *ct_out_len = 0;\n    *pt_out_len = 0;\n    r->alert_in = TLS_ALERT_NONE;\n",
              "    *ct_out_len = 0;\n    *pt_out_len = 0;\n    memset(out, 0xA5, TLS_STEP_OUT_MAX); // MUT2\n"
              "    r->alert_in = TLS_ALERT_NONE;\n")],
    # M3b: drop the final record: the last record read in the step that sees close_notify is not returned
    "m3b": [("    int pt_len = 0;\n", "    int pt_len = 0;\n    int last_n = 0; // M3b\n"),
            ("        if (n > 0) {\n            pt_len += n;\n",
             "        if (n > 0) {\n            pt_len += n;\n            last_n = n; // M3b\n"),
            ("        } else if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {\n            s->stats.state = TLS_STATE_CLOSED;\n",
             "        } else if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {\n            s->stats.state = TLS_STATE_CLOSED;\n"
             "            pt_len -= last_n; // M3b\n")],
    # M4b: tls_close() sends no close_notify of our own
    "m4b": [("        mbedtls_ssl_close_notify(&s->ssl);\n", "        // M4b: no mbedtls_ssl_close_notify()\n")],
    # M5: flip one bit of one plaintext byte at the 128 KiB mark of every session
    "m5": [("    s->stats.pt_out_bytes += pt_len;\n",
            "    if ((pt_len > 8) && (s->stats.pt_out_bytes < 131072) && (s->stats.pt_out_bytes + pt_len >= 131072)) {\n"
            "        out[ct_len + 4] ^= 0x20; // M5\n    }\n    s->stats.pt_out_bytes += pt_len;\n")],
    # M6b: accept a wrong host name under BUNDLE trust only
    "m6b": [("        if (esp_crt_verify_callback(NULL, crt, depth, flags) != 0) {\n",
             "        if (depth == 0) {\n            *flags &= ~MBEDTLS_X509_BADCERT_CN_MISMATCH; // M6b\n        }\n"
             "        if (esp_crt_verify_callback(NULL, crt, depth, flags) != 0) {\n")],
    # M8c: NO_MEM while handshaking: the handshake no longer runs on a refused step (the resend can never be taken)
    "m8c": [("        if (s->stats.state == TLS_STATE_HANDSHAKING) {\n            ret = tls_handshake(s, r);\n",
             "        if ((s->stats.state == TLS_STATE_HANDSHAKING) && !refused) { // M8c\n            ret = tls_handshake(s, r);\n")],
    # M10: a write silently keeps only the first 512 B of the plaintext
    "m10": [("        } else if (!refused && pt_in_len) {\n",
             "        } else if (!refused && pt_in_len) {\n            if (pt_in_len > 512) {\n"
             "                pt_in_len = 512; // M10\n            }\n")],
}

name, path = sys.argv[1], sys.argv[2]
text = open(path).read()
for old, new in MUTANTS[name]:
    if text.count(old) != 1:
        sys.exit("%s: the anchor is not found exactly once in %s: %r" % (name, path, old[:60]))
    text = text.replace(old, new)
open(path, "w").write(text)
print("applied", name)
