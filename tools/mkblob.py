#!/usr/bin/env python3
"""mkblob.py [out]: the local servers' 256 KiB download, deterministic: block i (32 B) is SHA-256 of i as 4 bytes
big-endian. Without out, prints its SHA-256 (certs/blob.h holds the same value for the app)."""
import hashlib, struct, sys

blob = b"".join(hashlib.sha256(struct.pack(">I", i)).digest() for i in range(8192))
if len(sys.argv) > 1:
    open(sys.argv[1], "wb").write(blob)
print(len(blob), hashlib.sha256(blob).hexdigest())
