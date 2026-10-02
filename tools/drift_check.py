#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""drift_check.py [engine_dir]: the required reds of tools/verify_transcriptions.py. Each drift mutant changes a COPY of the
engine checkout (the firmware drifted from what the app copied) or of the app (a copy drifted from its firmware
source), or a copied constant; verify_transcriptions.py must then fail. A comment-only firmware change must still pass. Exits 0 only if
all of that holds."""
import pathlib, shutil, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DRIFTS = [
    # name, where ("engine" or "app"), file, anchor, replacement, expected ("FAIL" or "ok")
    ("fw-step-overlay", "engine", "software/wifi/raw_u64/main/rpc_dispatch.c",
     "resp->data, &ct_out_len, &pt_out_len, &result);", "req->data, &ct_out_len, &pt_out_len, &result);", "FAIL"),
    ("fw-take-step-noread", "engine", "software/io/wifi/wifi_tls.cc",
     "if ((step->flags & WIFI_TLS_STEP_NO_READ) && (pt != 0)) {", "if (0) {", "FAIL"),
    ("fw-read-budget", "engine", "software/io/network/network_target.cc",
     "if ((steps >= TLS_READ_BUDGET_STEPS) || reached(budget_end) || reached(deadline)) {",
     "if ((steps >= 2 * TLS_READ_BUDGET_STEPS) || reached(budget_end) || reached(deadline)) {", "FAIL"),
    ("app-step-no-overlay", "app", "main/engine_task.c",
     "resp->data, &ct_out_len, &pt_out_len, &result);", "req->data + ct_in_len + pt_in_len, &ct_out_len, "
     "&pt_out_len, &result);", "FAIL"),
    ("app-take-step-noread", "app", "main/app.c",
     "if ((flags & TLS_STEP_NO_READ) && (pt != 0)) {", "if (0) {", "FAIL"),
    ("fw-ct-max", "engine", "software/io/network/network_target.h",
     "#define NET_TLS_CT_MAX     1536", "#define NET_TLS_CT_MAX     4096", "FAIL"),
    ("app-ct-room", "app", "main/app.c", "#define CT_ROOM          1536", "#define CT_ROOM          4096", "FAIL"),
    ("fw-comment-only (control)", "engine", "software/wifi/raw_u64/main/rpc_dispatch.c",
     "void cmd_tls_step(command_buf_t *buf)\n{", "void cmd_tls_step(command_buf_t *buf)\n{\n    // a reworded comment", "ok"),
]

ENGINE = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "engine"
failed = False
for name, where, path, old, new, expect in DRIFTS:
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        engine, app = tmp / "engine", tmp / "app"
        shutil.copytree(ENGINE, engine, symlinks=True, ignore=shutil.ignore_patterns(".git"))
        app.mkdir()
        for part in ("main", "tools"):
            shutil.copytree(ROOT / part, app / part)
        target = (engine if where == "engine" else app) / path
        text = target.read_text()
        if text.count(old) != 1:
            sys.exit("%s: anchor not found exactly once in %s" % (name, path))
        target.write_text(text.replace(old, new))
        r = subprocess.run([sys.executable, "-B", str(app / "tools/verify_transcriptions.py"), str(engine)],
                           capture_output=True, text=True)
        got = "ok" if r.returncode == 0 else "FAIL"
        first = next((l for l in r.stdout.splitlines() if "FAIL" in l), "all ok")
        verdict = "as required" if got == expect else "WRONG"
        failed |= got != expect
        print("%-28s %-6s verify=%-4s expected=%-4s %s | %s" % (name, where, got, expect, verdict, first))
sys.exit(1 if failed else 0)
