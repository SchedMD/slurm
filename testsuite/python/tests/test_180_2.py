############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmdbd HTTP endpoint content, authentication, and connection reuse."""

import json
import socket
from http.client import HTTPResponse

import pytest
import requests

import atf

VERSION = "v0.0.46"
ENDPOINTS = ("conf", "diag")
TIMEOUT = 30
PRIVILEGED_TEST_USER = (
    atf.properties["test-user-uid"] == 0
    or atf.properties["test-user"] == atf.properties["slurm-user"]
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmdbd",
        reason="Ticket 25218: slurmdbd HTTP endpoints require 26.11+",
    )
    atf.require_accounting()
    atf.require_config_parameter_excludes(
        "CommunicationParameters", "disable_http", source="slurmdbd"
    )
    for source in ("slurm", "slurmdbd"):
        atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt", source=source)
    atf.require_config_parameter(
        "HttpAuthParameters",
        f"jwt_key={atf.properties['slurm-config-dir']}/jwt_hs256.key",
        source="slurmdbd",
    )
    atf.require_slurm_running()
    atf.run_command(
        "sacctmgr -nP show cluster", user=atf.properties["slurm-user"], fatal=True
    )


@pytest.fixture
def versioned_endpoint():
    atf.require_version(
        (26, 11),
        "sbin/slurmdbd",
        max_version=(28, 11),
        reason=f"Ticket 25218: data_parser/{VERSION} requires 26.11 through 28.05",
    )


@pytest.fixture(scope="module")
def config():
    return {key.lower(): value for key, value in atf.get_config(source="dbd").items()}


@pytest.fixture(scope="module")
def target(config):
    return (
        config.get("dbdaddr") or config.get("dbdhost", "localhost"),
        int(config.get("dbdport", 6819)),
    )


@pytest.fixture(scope="module")
def url(target):
    host, port = target
    if ":" in host:
        host = f"[{host}]"
    return f"http://{host}:{port}"


@pytest.fixture(scope="module")
def headers():
    result = {}
    for user in {atf.properties["slurm-user"], atf.properties["test-user"]}:
        output = atf.run_command_output(
            f"scontrol token username={user}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        token = output.strip().splitlines()[-1].split("=", 1)[1]
        result[user] = {
            "X-SLURM-USER-NAME": user,
            "X-SLURM-USER-TOKEN": token,
            "Accept": "application/json",
        }
    return result


@pytest.mark.parametrize(
    "path, expected",
    [("/readyz", "READY"), ("/livez", "LIVE"), ("/healthz", "HEALTHY")],
)
def test_probe_content(url, path, expected):
    response = requests.get(f"{url}{path}", timeout=TIMEOUT)
    assert response.status_code == 200, response.text
    assert (
        response.text == expected
    ), f"Expected {expected!r} from {path}, got {response.text!r}"


def test_root_lists_endpoints(url):
    response = requests.get(f"{url}/", timeout=TIMEOUT)
    assert response.status_code == 200, response.text
    for path in (
        "/readyz",
        "/livez",
        "/healthz",
        "/{data_parser}/conf",
        "/{data_parser}/diag",
    ):
        assert path in response.text, f"Missing {path} from endpoint index"


@pytest.mark.usefixtures("versioned_endpoint")
@pytest.mark.parametrize("endpoint", ENDPOINTS)
@pytest.mark.parametrize("bad_token", [False, True], ids=["anonymous", "invalid-token"])
def test_versioned_endpoint_rejects_credentials(url, endpoint, bad_token):
    credentials = {}
    if bad_token:
        credentials = {"Authorization": "Bearer invalid-token"}
    response = requests.get(
        f"{url}/{VERSION}/{endpoint}", headers=credentials, timeout=TIMEOUT
    )
    assert response.status_code == 401, response.text


@pytest.mark.usefixtures("versioned_endpoint")
@pytest.mark.parametrize("endpoint", ENDPOINTS)
def test_versioned_endpoint_reuses_connection(target, headers, endpoint):
    """Like slurmctld metrics tests, require two replies on the same socket."""
    credentials = headers[atf.properties["slurm-user"]]
    with socket.create_connection(target, timeout=TIMEOUT) as sock:
        for close in (False, True):
            fields = [f"GET /{VERSION}/{endpoint} HTTP/1.1", "Host: localhost"]
            fields.extend(f"{key}: {value}" for key, value in credentials.items())
            if close:
                fields.append("Connection: close")
            sock.sendall(("\r\n".join(fields) + "\r\n\r\n").encode())
            with HTTPResponse(sock) as response:
                response.begin()
                assert response.status == 200, f"{endpoint}: HTTP {response.status}"
                assert (
                    response.will_close == close
                ), f"{endpoint}: unexpected close={response.will_close}"
                data = json.loads(response.read())
            assert not data.get("errors"), data
            if endpoint == "conf":
                assert (
                    data["slurmdbd_conf"]["DbdPort"] == target[1]
                ), f"Wrong listening port in configuration: {data}"
                assert data["slurmdbd_conf"][
                    "HttpAuthParameters"
                ], "Configured HTTP auth parameters missing from /conf"
            elif endpoint == "diag":
                assert (
                    data["statistics"]["time_start"] > 0
                ), "Statistics collection start time missing"
                assert data["statistics"]["RPCs"], "No RPC counters returned"
                assert data["statistics"]["users"], "No user counters returned"
        assert sock.recv(1) == b"", "slurmdbd ignored Connection: close"


@pytest.mark.usefixtures("versioned_endpoint")
def test_diag_averages(url, headers):
    response = requests.get(
        f"{url}/{VERSION}/diag",
        headers=headers[atf.properties["slurm-user"]],
        timeout=TIMEOUT,
    )
    assert response.status_code == 200, response.text
    stats = response.json()["statistics"]
    records = stats["RPCs"] + stats["users"]
    assert records, "No RPC or user statistics to validate"
    expected = []
    for record in records:
        count = record["count"]
        average = record["time"]["total"] // count if count else 0
        assert record["time"]["average"] == average, record
        expected.append(average)
    assert any(expected), "No nonzero RPC average was exercised"


@pytest.mark.usefixtures("versioned_endpoint")
@pytest.mark.parametrize("endpoint", ENDPOINTS)
@pytest.mark.skipif(
    PRIVILEGED_TEST_USER,
    reason="Ticket 25218: requires a non-root, non-SlurmUser test user",
)
def test_authenticated_user_access(url, headers, endpoint):
    user = atf.properties["test-user"]
    expected_status = 200
    if endpoint == "diag":
        level = (
            atf.run_command_output(
                f"sacctmgr -nP show user {user} format=AdminLevel",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
            .strip()
            .lower()
        )
        expected_status = 200 if "admin" in level else 403
    response = requests.get(
        f"{url}/{VERSION}/{endpoint}", headers=headers[user], timeout=TIMEOUT
    )
    assert response.status_code == expected_status, response.text


def test_verbose_readyz_requires_authentication(url):
    response = requests.get(f"{url}/readyz?verbose", timeout=TIMEOUT)
    assert response.status_code == 401, response.text


def test_verbose_readyz_for_slurm_user(url, headers):
    response = requests.get(
        f"{url}/readyz?verbose",
        headers=headers[atf.properties["slurm-user"]],
        timeout=TIMEOUT,
    )
    assert response.status_code == 200, response.text
    assert response.text, "Verbose readiness returned an empty body"
    assert response.text != "READY", "Verbose readiness returned the normal body"


@pytest.mark.skipif(
    PRIVILEGED_TEST_USER,
    reason="Ticket 25218: requires a non-root, non-SlurmUser test user",
)
def test_verbose_readyz_rejects_regular_user(url, headers):
    response = requests.get(
        f"{url}/readyz?verbose",
        headers=headers[atf.properties["test-user"]],
        timeout=TIMEOUT,
    )
    assert response.status_code == 403, response.text
