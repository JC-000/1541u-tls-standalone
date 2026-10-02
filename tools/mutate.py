#!/usr/bin/env python3
"""mutate.py <mutant> <tls_engine.c>: applies one known-bad change to a COPY of the engine. Each must turn the run RED.
mut1: drops all plaintext after the first 4 KiB of a session (the gate's byte counts and SHA-256 must catch it).
mut2: writes the step output before the input is consumed; the bridge overlays them (rpc_dispatch.c:407-416), so
      the input is corrupted (the bridge-shaped buffer must catch it).
Both are standalone-adversarial1's MUT1/MUT2."""
import sys

MUTANTS = {
    "mut1": ("    *pt_out_len = pt_len;\n",
             "    *pt_out_len = (s->stats.pt_out_bytes > 4096) ? 0 : pt_len; // MUT1\n"),
    "mut2": ("    *ct_out_len = 0;\n    *pt_out_len = 0;\n    r->alert_in = TLS_ALERT_NONE;\n",
             "    *ct_out_len = 0;\n    *pt_out_len = 0;\n    memset(out, 0xA5, TLS_STEP_OUT_MAX); // MUT2\n"
             "    r->alert_in = TLS_ALERT_NONE;\n"),
}
name, path = sys.argv[1], sys.argv[2]
old, new = MUTANTS[name]
text = open(path).read()
if text.count(old) != 1:
    sys.exit("%s: the anchor is not found exactly once in %s" % (name, path))
open(path, "w").write(text.replace(old, new))
print("applied", name)
