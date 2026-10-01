#!/usr/bin/env python3
"""results.py <results.txt>: the RESULT/CASE lines of a run as a Markdown table."""
import re, shlex, sys

rows, verdict = [], {}
for line in open(sys.argv[1]):
    if line.startswith("RESULT "):
        rows.append(dict(kv.split("=", 1) for kv in shlex.split(line[7:])))
    elif line.startswith("CASE "):
        name, rest = line[5:].split(" ", 1)
        verdict.setdefault(name, []).append(rest.strip())

VER = {"0x0304": "1.3", "0x0303": "1.2", "0x0000": "-"}
GROUP = {"0x001d": "X25519", "0x0017": "P-256", "0x0018": "P-384", "0x0019": "P-521", "0x0000": "-"}
print("| case | verdict | outcome | TLS | suite | group | handshake wall ms | mbedTLS hs ms | plaintext B "
      "| transfer ms | KiB/s wall | KiB/s engine | longest step ms | heap peak use B | heap min free B "
      "| largest alloc B | stack free B (of 8192) |")
print("|" + "---|" * 17)
for r in rows:
    pt, xfer, eng = int(r["pt_bytes"]), int(r["xfer_ms"]), float(r["xfer_engine_ms"])
    refused = r["outcome"] == "open-refused"
    v = verdict.get(r["case"], ["?"]).pop(0)
    print("| %s | %s | %s | %s | %s | %s | %s | %s | %d | %s | %s | %s | %s | %s | %s | %s | %s |" % (
        r["case"], v.split(" ", 1)[0] + " " + v.split(" ", 1)[1].replace("|", "/") if " " in v else v,
        r["outcome"] + ("" if r["fail_reason"] == "0" else " (reason %s, verify %s)" % (r["fail_reason"], r["verify"])),
        VER.get(r["version"], r["version"]), r["suite"], GROUP.get(r["group"], r["group"]),
        r["hs_wall_ms"] if r["hs_wall_ms"] != "0" else "-", r["hs_mbedtls_ms"] if r["version"] != "0x0000" else "-",
        pt, xfer or "-", "%.1f" % (pt / 1024 / (xfer / 1000)) if xfer and pt > 4096 else "-",
        "%.1f" % (pt / 1024 / (eng / 1000)) if eng and pt > 4096 else "-", r["step_max_ms"],
        "-" if refused else r["heap_peak_use"], "-" if refused else r["heap_min"], r["largest_alloc"],
        r["stack_free"]))
