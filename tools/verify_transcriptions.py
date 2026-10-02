#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""verify_transcriptions.py [--update] [engine_dir]: the app carries hand copies of firmware code it cannot compile
here. This checks them against the engine commit's own sources, with comments, blank lines and indentation ignored:

  1. each firmware region (below) must equal the reference stored in tools/transcriptions/ (the text the copy was
     made from); a difference means the firmware changed and the copy must be reviewed again;
  2. where the copy is a transcription, the diff from that reference to the copy must equal the stored declared
     delta (tools/transcriptions/*.delta); anything else is an undeclared change.

network_target.cc's pump is restructured in app.c rather than transcribed, so it is checked by (1) only: its firmware
source is pinned, the port itself is not compared. The constants the app copies (buffer sizes, the write budget, the
TLS task's stack) are pinned too, and each must equal the app's own value.
A reference starts with "# " header lines (its origin and licence), which are not compared.
--update rewrites the references and deltas from the current trees (only after reviewing them)."""
import difflib, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REF = ROOT / "tools/transcriptions"
CHECKS = [
    # name, firmware file, firmware functions, app file, app functions (None: reference only)
    ("rpc_dispatch", "software/wifi/raw_u64/main/rpc_dispatch.c",
     ["cmd_tls_open", "cmd_tls_step", "cmd_tls_close", "cmd_tls_stats", "cmd_tls_close_all", "cmd_tls_set_ca",
      "tls_serve"],
     "main/engine_task.c",
     ["cmd_tls_open", "cmd_tls_step", "cmd_tls_close", "cmd_tls_stats", "cmd_tls_close_all", "cmd_tls_set_ca",
      "serve"]),
    ("wifi_tls", "software/io/wifi/wifi_tls.cc", ["is_answer", "bare_refusal", "wifi_tls_take_step"],
     "main/app.c", ["take_step"]),
    ("network_target", "software/io/network/network_target.cc",
     ["tls_run_step", "tls_send_all", "tls_handshake", "tls_read", "tls_write", "tls_close_entry"], None, None),
]
# Constants: firmware file, firmware #define, app file, app #define (None: pinned only)
CONSTANTS = [
    ("software/io/network/network_target.h", "NET_TLS_CT_MAX", "main/app.c", "CT_ROOM"),
    ("software/io/network/network_target.h", "NET_TLS_PT_MAX", "main/app.c", "PT_ROOM"),
    ("software/io/network/network_target.cc", "TLS_WRITE_BUDGET", "main/app.c", "WRITE_BUDGET"),
    ("software/io/network/network_target.cc", "TLS_READ_BUDGET_STEPS", None, None),
    ("software/wifi/raw_u64/main/rpc_dispatch.h", "TLS_TASK_STACK", "main/engine_task.h", "TLS_TASK_STACK"),
]
HEADER = ("# Origin: %s of the 1541 Ultimate firmware (GideonZ/1541ultimate), GPL-3.0; comments and layout removed.\n"
          "# Written by tools/verify_transcriptions.py --update; the reference the app's copy was made from.\n")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def function(text, name, path):
    """The definition of name: from its line at column 0 to the matching closing brace."""
    m = re.search(r"^[^\s#/].*\b%s\s*\([^;]*?$" % re.escape(name), text, re.M)
    if not m:
        sys.exit("verify: %s: no definition of %s" % (path, name))
    start = text.index("{", m.start())
    depth = 0
    for i in range(start, len(text)):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            return text[m.start():i + 1]
    sys.exit("verify: %s: %s does not end" % (path, name))


def define(path, name):
    m = re.search(r"^#define\s+%s\s+(\S+)" % re.escape(name), strip_comments(path.read_text()), re.M)
    if not m:
        sys.exit("verify: %s: no #define %s" % (path, name))
    return m.group(1)


def body(text):
    return "".join(l for l in text.splitlines(True) if not l.startswith("# "))


def region(path, names):
    text = strip_comments(path.read_text())
    lines = []
    for name in names:
        lines += [" ".join(l.split()) for l in function(text, name, path).splitlines()]
    return "\n".join(l for l in lines if l) + "\n"


def main():
    args = [a for a in sys.argv[1:] if a != "--update"]
    update = "--update" in sys.argv
    engine = pathlib.Path(args[0]) if args else ROOT / "engine"
    failed = False
    for name, fw, fw_names, app, app_names in CHECKS:
        fw_path = engine / fw
        if not fw_path.exists():
            print("verify: FAIL %s: %s is not in this engine commit, so the copy cannot be checked" % (name, fw))
            failed = True
            continue
        current = region(fw_path, fw_names)
        ref_path = REF / ("%s.ref" % name)
        if update:
            ref_path.write_text(HEADER % fw + current)
        if current != body(ref_path.read_text()):
            print("verify: FAIL %s: %s changed against the reference the copy was made from:" % (name, fw))
            sys.stdout.writelines(difflib.unified_diff(body(ref_path.read_text()).splitlines(True),
                                                       current.splitlines(True), "reference", fw))
            failed = True
            continue
        if not app:
            print("verify: ok %s: %s equals its reference (ported, not transcribed: review app.c if it changes)"
                  % (name, fw))
            continue
        delta = "".join(difflib.unified_diff(current.splitlines(True), region(ROOT / app, app_names).splitlines(True),
                                             fw, app, n=0))
        delta_path = REF / ("%s.delta" % name)
        if update:
            delta_path.write_text(delta)
        if delta != delta_path.read_text():
            print("verify: FAIL %s: %s differs from %s by more than the declared delta %s"
                  % (name, app, fw, delta_path.relative_to(ROOT)))
            sys.stdout.writelines(difflib.unified_diff(delta_path.read_text().splitlines(True),
                                                       delta.splitlines(True), "declared", "actual"))
            failed = True
            continue
        print("verify: ok %s: %s = %s + the declared delta (%d lines)" % (name, app, fw, delta.count("\n")))
    pinned = ["%s %s %s" % (fw, name, define(engine / fw, name) if (engine / fw).exists() else "ABSENT")
              for fw, name, _, _ in CONSTANTS]
    pin_path = REF / "constants.ref"
    if update:
        pin_path.write_text("# The firmware constants the app copies (tools/verify_transcriptions.py --update).\n"
                            + "\n".join(pinned) + "\n")
    if "\n".join(pinned) + "\n" != body(pin_path.read_text()):
        print("verify: FAIL constants: the firmware's constants changed against %s:" % pin_path.relative_to(ROOT))
        sys.stdout.writelines(difflib.unified_diff(body(pin_path.read_text()).splitlines(True),
                                                   ("\n".join(pinned) + "\n").splitlines(True), "reference", "engine"))
        failed = True
    else:
        mismatch = [(fw, name, app, app_name) for fw, name, app, app_name in CONSTANTS
                    if app and define(engine / fw, name) != define(ROOT / app, app_name)]
        for fw, name, app, app_name in mismatch:
            print("verify: FAIL constants: %s %s = %s, but %s %s = %s" % (fw, name, define(engine / fw, name), app,
                                                                       app_name, define(ROOT / app, app_name)))
        failed |= bool(mismatch)
        if not mismatch:
            print("verify: ok constants: %d firmware constants equal their reference and the app's copies"
                  % len(CONSTANTS))
    sys.exit(1 if failed else 0)


main()
