############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test HTTP connection reuse on slurmctld and slurmd (Issue 51078).

Send each request only after reading the preceding response on the same raw
socket. HTTPResponse parses replies but cannot reconnect the socket, so an
endpoint that closes prematurely cannot pass through client reconnection.
The last request asks for close, providing an EOF control on every path.
"""

import socket
from http.client import HTTPResponse

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_excludes("CommunicationParameters", "disable_http")
    atf.require_slurm_running()


@pytest.fixture(scope="module", params=["slurmctld", "slurmd"])
def daemon(request):
    return request.param


@pytest.fixture(scope="module")
def probe_status(daemon):
    """Check the target daemon's version, not the mixed-version cluster's."""

    return 200 if atf.get_version(f"sbin/{daemon}") >= (26, 11) else 204


@pytest.fixture(scope="module")
def target(daemon):
    """Return the selected daemon's listening address without involving restd."""

    atf.require_version(
        (26, 11),
        f"sbin/{daemon}",
        reason="Issue 51078: daemon HTTP endpoint connection reuse requires 26.11+",
    )
    if daemon == "slurmctld":
        port = str(atf.properties["slurmctld_port"]).split("-")[0]
        return atf.properties["slurmctld_host"], int(port)

    node = next(iter(atf.get_nodes().values()))
    return node["address"] or node["name"], node["port"]


@pytest.mark.parametrize(
    "path, code",
    [
        ("/", 200),
        ("/healthz", None),
        ("/livez", None),
        ("/readyz", None),
        ("/not-a-slurm-endpoint", 404),
    ],
)
def test_endpoint_reuses_connection(target, probe_status, path, code):
    """Verify each daemon handler allows a second exchange on the same socket."""

    if code is None:
        code = probe_status
    with socket.create_connection(target, timeout=30) as sock:
        for close in (False, True):
            connection = "Connection: close\r\n" if close else ""
            sock.sendall(
                f"GET {path} HTTP/1.1\r\nHost: localhost\r\n{connection}\r\n".encode()
            )
            with HTTPResponse(sock) as response:
                response.begin()
                assert (
                    response.status == code
                ), f"GET {path} on {target} returned {response.status}, expected {code}"
                assert (
                    response.will_close == close
                ), f"GET {path} on {target} has unexpected close={response.will_close}"
                response.read()
        assert sock.recv(1) == b"", f"Expected {target} to honor Connection: close"


def test_http_10_without_keep_alive_closes(target, probe_status):
    """Verify the raw client observes EOF when persistence was not negotiated."""

    with socket.create_connection(target, timeout=30) as sock:
        sock.sendall(b"GET /healthz HTTP/1.0\r\nHost: localhost\r\n\r\n")
        with HTTPResponse(sock) as response:
            response.begin()
            assert (
                response.status == probe_status
            ), f"HTTP/1.0 probe on {target} returned {response.status}"
            assert response.will_close, f"HTTP/1.0 probe on {target} should close"
            response.read()
        assert sock.recv(1) == b"", f"Expected HTTP/1.0 EOF from {target}"
