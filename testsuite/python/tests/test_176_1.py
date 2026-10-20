############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmdbd connection read timeouts and short-header timeouts."""

import logging
import os
import socket
import ssl
import struct
import subprocess
import time

import pytest

import atf

# Scheduling and teardown slack.
GRACE_SECONDS = 15
ANNOUNCED_PAYLOAD = 64


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Require accounting."""
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def dbd_config():
    """Use slurmdbd's config, not slurmctld's."""
    return {key.lower(): value for key, value in atf.get_config(source="dbd").items()}


@pytest.fixture(scope="module")
def dbd_address(dbd_config):
    return (
        dbd_config.get("dbdaddr") or dbd_config.get("dbdhost", "localhost"),
        int(dbd_config.get("dbdport", 6819)),
    )


@pytest.fixture(scope="module")
def message_timeout(dbd_config):
    return int(str(dbd_config.get("messagetimeout", 10)).split()[0])


@pytest.fixture(scope="module")
def tls_enabled(dbd_config):
    tls_type = dbd_config.get("tlstype")
    return tls_type is not None and str(tls_type).lower() not in (
        "tls/none",
        "none",
        "(null)",
    )


@pytest.fixture(scope="module")
def tls_context():
    """Test TLS transport, not certificate identity."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    return context


@pytest.fixture
def dbd_socket(dbd_address, tls_enabled, tls_context, message_timeout):
    """Connect, completing TLS if required."""
    sock = socket.create_connection(
        dbd_address, timeout=message_timeout + GRACE_SECONDS
    )
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if tls_enabled:
            sock = tls_context.wrap_socket(sock, server_hostname=dbd_address[0])
        yield sock
    finally:
        sock.close()


def _peer_closed(sock, wait_seconds):
    """Wait for EOF or reset."""
    deadline = time.monotonic() + wait_seconds
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        sock.settimeout(remaining)
        try:
            if sock.recv(1) == b"":
                return True
        except socket.timeout:
            return False
        except (ConnectionResetError, ssl.SSLEOFError):
            return True


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Issue 50454: old slurmdbd waited indefinitely for the first RPC",
)
@pytest.mark.parametrize("payload_bytes", [None, ANNOUNCED_PAYLOAD // 2])
def test_unidentified_connection_times_out(dbd_socket, message_timeout, payload_bytes):
    """Like slurmctld, wait MessageTimeout for a connection's first RPC."""
    if payload_bytes is not None:
        dbd_socket.sendall(struct.pack("!I", ANNOUNCED_PAYLOAD) + b"\0" * payload_bytes)

    wait = message_timeout + GRACE_SECONDS
    logging.info(f"Observing payload_bytes={payload_bytes} for {wait} seconds")
    assert _peer_closed(
        dbd_socket, wait
    ), f"slurmdbd kept an unidentified connection open for {wait} seconds"


@pytest.mark.parametrize("header_bytes", [1, 2, 3])
def test_short_header_times_out(dbd_socket, message_timeout, header_bytes):
    """A partial length header never completes; the connection times out."""
    dbd_socket.sendall(struct.pack("!I", ANNOUNCED_PAYLOAD)[:header_bytes])
    assert _peer_closed(
        dbd_socket, message_timeout + GRACE_SECONDS
    ), f"slurmdbd accepted a {header_bytes}-byte length header"


@pytest.fixture
def sacctmgr_session():
    """Interactive sacctmgr, which keeps one persistent connection."""
    atf.require_version(
        (25, 5),
        "bin/sacctmgr",
        reason="Issue 50039: sacctmgr logs 'Sending message' with Network debug flag since 25.05",
    )

    # atf.run_command() runs one command; this needs one session for two.
    session = subprocess.Popen(
        # -v so the Network debug flag logs each connection.
        [atf.properties["slurm-bin-dir"] + "/sacctmgr", "-v"],
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        env=dict(os.environ, SLURM_DEBUG_FLAGS="Network"),
    )
    yield session
    if session.poll() is None:
        session.kill()
        session.wait()


def test_persistent_connection_has_no_read_timeout(message_timeout, sacctmgr_session):
    """An idle persistent connection outlives MessageTimeout."""
    wait = message_timeout + GRACE_SECONDS

    # Opens the persistent connection, then leaves it idle.
    sacctmgr_session.stdin.write("show cluster format=cluster\n")
    sacctmgr_session.stdin.flush()

    # Idling past MessageTimeout is the condition under test.
    logging.info(f"Leaving a persistent connection idle for {wait} seconds")
    time.sleep(wait)

    # If slurmdbd closed it, sacctmgr must reopen it for this query.
    sacctmgr_session.stdin.write("show cluster format=cluster\nquit\n")
    sacctmgr_session.stdin.flush()
    _, stderr = sacctmgr_session.communicate(timeout=60)

    assert sacctmgr_session.returncode == 0, f"sacctmgr failed: {stderr}"
    assert (
        stderr.count("Sending message REQUEST_PERSIST_INIT") == 1
    ), f"sacctmgr had to reconnect after {wait} idle seconds: {stderr}"
