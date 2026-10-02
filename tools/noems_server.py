#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""noems_server.py <port> <cert> <key>: a TLS 1.2 server WITHOUT the extended master secret (RFC 7627), which the
container's OpenSSL 3.0 s_server cannot be told to drop. SSL_OP_NO_EXTENDED_MASTER_SECRET is bit 0 in OpenSSL 3."""
import socket, ssl, sys

port, cert, key = int(sys.argv[1]), sys.argv[2], sys.argv[3]
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ctx.maximum_version = ssl.TLSVersion.TLSv1_2
ctx.load_cert_chain(cert, key)
ctx.options |= 0x1
srv = socket.create_server(("0.0.0.0", port))
while True:
    conn, peer = srv.accept()
    try:
        with ctx.wrap_socket(conn, server_side=True) as tls:
            tls.recv(4096)
            tls.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nno-ems\n")
    except Exception as e:
        print("peer %s: %s" % (peer[0], e), flush=True)
