############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test metrics endpoint connection reuse with empty and populated replies.

Issue 51078 changes the metrics index and serialized replies independently.
Exercise both without a reconnecting client, including the empty responses
when MetricsType is unset. The configured HTTP parser is left unchanged so
parser variants exercise the same wire-level contract.
"""

import socket
from http.client import HTTPResponse

import pytest

import atf


@pytest.fixture(
    scope="module",
    autouse=True,
    params=[None, "metrics/openmetrics"],
    ids=["disabled", "openmetrics"],
)
def setup(request):
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 51078: metrics endpoint connection reuse requires 26.11+",
    )
    # Older clients and nodes cannot parse the shared MetricsType setting.
    for component in ("bin/scontrol", "sbin/slurmd"):
        atf.require_version(
            (25, 11),
            component,
            reason="Issue 51078: MetricsType configuration requires 25.11+",
        )
    # Metrics plugins are initialized at startup, not on reconfigure.
    if atf.properties["auto-config"] and atf.is_slurmctld_running(quiet=True):
        atf.stop_slurm()
    atf.require_config_parameter_excludes("CommunicationParameters", "disable_http")
    atf.require_config_parameter("MetricsType", request.param)
    atf.require_config_parameter("MetricsAuthUsers", None)
    atf.require_config_parameter("PrivateData", None)
    atf.require_slurm_running()
    return request.param


@pytest.mark.parametrize(
    "path",
    [
        "/metrics",
        "/metrics/jobs",
        "/metrics/nodes",
        "/metrics/partitions",
        "/metrics/scheduler",
        "/metrics/jobs-users-accts",
    ],
)
def test_metrics_reuses_connection(setup, path):
    """Verify both index and data handlers preserve the first connection."""

    port = str(atf.properties["slurmctld_port"]).split("-")[0]
    target = atf.properties["slurmctld_host"], int(port)
    with socket.create_connection(target, timeout=30) as sock:
        for close in (False, True):
            connection = "Connection: close\r\n" if close else ""
            sock.sendall(
                f"GET {path} HTTP/1.1\r\nHost: localhost\r\n{connection}\r\n".encode()
            )
            with HTTPResponse(sock) as response:
                response.begin()
                assert (
                    response.status == 200
                ), f"GET {path} returned {response.status}, expected 200"
                assert (
                    response.will_close == close
                ), f"GET {path} has unexpected close={response.will_close}"
                body = response.read()
                if setup is None and path != "/metrics":
                    assert not body, f"Disabled metrics should be empty, got {body!r}"
                elif path != "/metrics/jobs-users-accts":
                    # Per-user/account output may be empty when no jobs exist.
                    assert body, f"Expected metrics content from {path}"
        assert sock.recv(1) == b"", "Expected metrics EOF after Connection: close"
