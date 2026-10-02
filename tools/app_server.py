#!/usr/bin/env python3
"""app_server.py <port> <status_port> <cert> <key>: a TLS 1.3 server for the two cases s_server cannot serve.
  GET /keepalive  answers 5 bytes with Content-Length and keeps the connection open, then records how the client
                  ended it: "notify" (a close_notify arrived), "no-notify: ..." (TCP end without one) or "timeout".
  POST /echo      reads Content-Length bytes and answers the SHA-256 (hex + LF) of what it read.
The status port (plain TCP) answers "<connection number> <last /keepalive result>" and closes."""
import socket, ssl, sys, threading, hashlib

port, status_port, cert, key = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ssl.TLSVersion.TLSv1_3
ctx.load_cert_chain(cert, key)
state = {"n": 0, "last": "none"}

def status():
    srv = socket.create_server(("0.0.0.0", status_port))
    while True:
        c, _ = srv.accept()
        c.sendall(("%d %s\n" % (state["n"], state["last"])).encode())
        c.close()

def read_head(tls):
    head = b""
    while b"\r\n\r\n" not in head:
        d = tls.recv(4096)
        if not d:
            raise EOFError("closed in the request head")
        head += d
    return head.split(b"\r\n\r\n", 1)

threading.Thread(target=status, daemon=True).start()
srv = socket.create_server(("0.0.0.0", port))
while True:
    raw, peer = srv.accept()
    raw.settimeout(15)
    try:
        tls = ctx.wrap_socket(raw, server_side=True, suppress_ragged_eofs=False)
        head, body = read_head(tls)
        line = head.split(b"\r\n", 1)[0]
        if line.startswith(b"GET /keepalive"):
            tls.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: keep-alive\r\n\r\nhello")
            try:
                rest = tls.recv(4096)
                result = "notify" if rest == b"" else "data: %r" % rest[:20]
            except ssl.SSLEOFError as e:
                result = "no-notify: %s" % e
            except socket.timeout:
                result = "timeout"
            except (ssl.SSLError, OSError) as e:
                result = "no-notify: %s" % e
            state["n"] += 1
            state["last"] = result
            print("keepalive", state["n"], result, flush=True)
        elif line.startswith(b"POST /echo"):
            length = 0
            for h in head.split(b"\r\n")[1:]:
                if h.lower().startswith(b"content-length:"):
                    length = int(h.split(b":", 1)[1])
            while len(body) < length:
                d = tls.recv(65536)
                if not d:
                    break
                body += d
            digest = (hashlib.sha256(body[:length]).hexdigest() + "\n").encode()
            print("echo", length, len(body), digest.decode().strip(), flush=True)
            tls.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n" % len(digest) + digest)
            tls.settimeout(2)
            try:
                tls.unwrap()  # sends our close_notify
            except (ssl.SSLError, OSError, socket.timeout):
                pass
        tls.close()
    except Exception as e:
        print("peer %s: %r" % (peer[0], e), flush=True)
        raw.close()
