############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmdbd TLS negotiation timeouts."""

import socket
import ssl

import pytest

import atf

# Scheduling and teardown slack.
GRACE_SECONDS = 15

# Do not auto-configure TLS: it needs certificate setup.
pytestmark = pytest.mark.skipif(
    not atf.get_config_parameter(
        "TLSType", default=None, live=False, source="slurmdbd", quiet=True
    ),
    reason="Issue 51066: requires TLSType in slurmdbd.conf and slurm.conf",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Require TLS and accounting."""
    atf.require_tls()
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


def _peer_closed(sock, wait_seconds):
    """Return whether the peer closes the socket within wait_seconds."""
    sock.settimeout(wait_seconds)
    try:
        return sock.recv(1) == b""
    except socket.timeout:
        return False
    except ConnectionResetError:
        return True


@pytest.mark.parametrize("partial_hello", [False, True])
def test_stalled_tls_negotiation_times_out(dbd_address, message_timeout, partial_hello):
    """Time out silent peers and partial ClientHello messages."""
    with socket.create_connection(
        dbd_address, timeout=message_timeout + GRACE_SECONDS
    ) as sock:
        if partial_hello:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            outgoing = ssl.MemoryBIO()
            client = context.wrap_bio(
                ssl.MemoryBIO(), outgoing, server_hostname=dbd_address[0]
            )
            with pytest.raises(ssl.SSLWantReadError):
                client.do_handshake()
            hello = outgoing.read()
            assert len(hello) > 6, "TLS did not produce a complete ClientHello"
            # TLS record header plus one payload byte.
            sock.sendall(hello[:6])

        assert _peer_closed(
            sock, message_timeout + GRACE_SECONDS
        ), "slurmdbd did not close a stalled TLS negotiation"
