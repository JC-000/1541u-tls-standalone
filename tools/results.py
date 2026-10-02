#!/usr/bin/env python3
"""results.py <results.txt>: the RESULT/CASE lines of a run as a Markdown table."""
import re, shlex, sys

rows, verdict, tail = [], {}, []
for line in open(sys.argv[1]):
    if line.startswith("RESULT "):
        rows.append(dict(kv.split("=", 1) for kv in shlex.split(line[7:])))
    elif line.startswith("CASE "):
        name, gate, rest = line[5:].split(" ", 2)
        verdict.setdefault(name, []).append(rest.strip())
    elif line.startswith(("SUMMARY", "VERDICT", "*** MUTANT")):
        tail.append(line.strip())

VER = {"0x0304": "1.3", "0x0303": "1.2", "0x0000": "-"}
GROUP = {"0x001d": "X25519", "0x0017": "P-256", "0x0018": "P-384", "0x0019": "P-521", "0x0000": "-"}
print("| case | gate | verdict | end | TLS | suite | group | handshake wall ms | mbedTLS hs ms | body B "
      "| transfer ms | KiB/s engine | steps (again) | longest step ms | heap peak use B | largest alloc B "
      "| stack free B (of 8192) |")
print("|" + "---|" * 17)
for r in rows:
    body, xfer, eng = int(r["body_bytes"]), int(r["xfer_ms"]), float(r["xfer_engine_ms"])
    refused = r["error"] == "open-refused"
    v = verdict.get(r["case"], ["?"]).pop(0).replace("|", "/")
    end = r["end"] if r["error"] == "-" else "%s (%s)" % (r["end"], r["error"])
    print("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %d | %s | %s | %s (%s) | %s | %s | %s | %s |" % (
        r["case"], "gate" if r["gate"] == "1" else "info", v, end, VER.get(r["version"], r["version"]), r["suite"],
        GROUP.get(r["group"], r["group"]), r["hs_wall_ms"] if r["hs_wall_ms"] != "0" else "-",
        r["hs_mbedtls_ms"] if r["version"] != "0x0000" else "-", body, xfer or "-",
        "%.1f" % (body / 1024 / (eng / 1000)) if eng and body > 4096 else "-", r["steps"], r["again_steps"],
        r["step_max_ms"], "-" if refused else r["heap_peak_use"], r["largest_alloc"], r["stack_free"]))
print()
for t in tail:
    print("`%s`  " % t)
