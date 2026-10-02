#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""sntp_server.py [port]: a minimal SNTP server (RFC 4330) answering with this machine's clock, so a QEMU run gets
its time from the host instead of a public NTP server. Port 123 by default (needs root, as in the container)."""
import socket, struct, sys, time

NTP_EPOCH = 2208988800
def ts(t):
    return struct.pack(">II", int(t) + NTP_EPOCH, int((t % 1) * (1 << 32)))

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", int(sys.argv[1]) if len(sys.argv) > 1 else 123))
while True:
    data, peer = s.recvfrom(512)
    if len(data) < 48:
        continue
    now = time.time()
    reply = bytes([0x24, 1, data[2], 0xEC]) + b"\0" * 8 + b"LOCL" + ts(now) + data[40:48] + ts(now) + ts(time.time())
    s.sendto(reply, peer)
    print("answered", peer[0], flush=True)
