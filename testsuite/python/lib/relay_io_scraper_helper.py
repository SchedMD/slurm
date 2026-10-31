############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Bind the relay socket and write down what arrives, for the relay_io tests.

Run as root by relay_io_scraper.Scraper rather than imported. The daemons
refuse to send to a socket owned by anyone but root or SlurmdUser, so the
stand in scraper cannot be a thread inside pytest, which runs as an
ordinary user.

Each datagram is one record. It is written to the output file base64
encoded, one per line, so that a record holding NUL bytes, invalid UTF-8 or
a newline still arrives as exactly one line for the test to read back.

Usage: relay_io_scraper_helper.py SOCKET OUTPUT PIDFILE RECV_SIZE RCVBUF OWNER

OWNER is the uid to give the socket, or -1 to leave it owned by root.
"""

import base64
import os
import socket
import sys


def main():
    path, out_path, pid_path, recv_size, rcvbuf, owner = sys.argv[1:7]
    recv_size = int(recv_size)
    rcvbuf = int(rcvbuf)
    owner = int(owner)

    if os.path.exists(path):
        os.unlink(path)

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(path)
    if rcvbuf:
        # Raises the byte ceiling only: the kernel still queues just
        # net.unix.max_dgram_qlen datagrams, which SO_RCVBUF cannot change. A
        # burst is absorbed by the sender's ring, which stepd_ring_mbytes
        # pins: on EAGAIN the writer leaves the record queued rather than
        # dropping it, and only evicts once that ring is full.
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
    # what the relay_io guide recommends: only root may send to it
    os.chmod(path, 0o600)
    os.chown(path, owner, -1)

    # Unbuffered, so that a record is readable as soon as it arrives rather
    # than when enough of them have.
    out = open(out_path, "ab", buffering=0)
    os.chmod(out_path, 0o666)

    # Written only once the socket is bound and the output file exists, so
    # that the test waiting on it knows the relay has somewhere to go.
    # Renamed into place so that the test never reads it half written.
    tmp_path = f"{pid_path}.tmp"
    with open(tmp_path, "w") as f:
        f.write(f"{os.getpid()}\n")
    os.chmod(tmp_path, 0o666)
    os.rename(tmp_path, pid_path)

    while True:
        # One datagram is one record: the message boundary delimits it, so
        # there is nothing to reassemble and nothing to split on.
        data = sock.recv(recv_size)
        if not data:
            continue
        # One write, so that a reader never sees half a line
        out.write(base64.b64encode(data) + b"\n")


if __name__ == "__main__":
    main()
