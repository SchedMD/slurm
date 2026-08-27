############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify slurmctld rejects non-TLS RPCs when it is not serving HTTP.

Companion to test_166_1, which covers the default configuration. There,
slurmctld's listening socket is handed to the HTTP switch and a non-TLS RPC is
refused on the way through it. With CommunicationParameters=disable_http the
switch is out of the picture and the socket is a plain RPC listener, so the
refusal has to come from the conmgr RPC path instead.

That is the configuration in which the conmgr client-listener reject is what
stands between a plaintext client and slurmctld, so it gates on the release
carrying that reject rather than on the older HTTP-switch one.
"""

import os

import pytest
import requests

import atf

REASON = "Issue 51066: conmgr RPC reject added in 26.05.4"
# slurm_errno.h. What slurmd answers an unprivileged from_slurmctld RPC.
ESLURM_USER_ID_MISSING = 2010


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_tls()
    atf.require_tool("gcc")
    atf.require_config_parameter_includes("CommunicationParameters", "disable_http")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def non_tls_conf(module_setup):
    """Path to a slurm.conf that includes the real config but disables TLS.

    Absolute because the tests consuming it run in their own directory.
    """
    real_conf = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    path = "non_tls_slurm.conf"
    atf.run_command(
        f"printf 'include {real_conf}\\nTLSType=tls/none\\n' > {path}",
        fatal=True,
    )
    return os.path.abspath(path)


@pytest.fixture(scope="module")
def slurmctld_target():
    """The "<host> <port>" probe argument for the slurmctld RPC listener."""
    port = str(atf.properties["slurmctld_port"]).split("-")[0]
    return f"{atf.properties['slurmctld_host']} {port}"


@pytest.fixture(scope="module")
def slurmd_target():
    """The "<host> <port>" probe argument for a slurmd RPC listener."""
    node = sorted(atf.get_nodes())[0]
    host = atf.get_node_parameter(node, "address") or node
    port = atf.get_node_parameter(node, "port")
    return f"{host} {port}"


@pytest.fixture(scope="module")
def probe(module_setup):
    """Compile the non-TLS probe helper and return its absolute path."""
    source = f"{atf.properties['testsuite_scripts_dir']}/tls_reject_probe.c"
    dest = "tls_reject_probe"
    atf.compile_against_libslurm(source, dest, full=True, fatal=True)
    return os.path.abspath(dest)


@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (26, 5, 4), reason=REASON)
def test_slurmctld_rejects_non_tls_without_http(slurmctld_target, non_tls_conf, probe):
    """A non-TLS client is rejected with HTTP listening disabled."""
    result = atf.run_command(
        f"{probe} {slurmctld_target}",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in output
    ), f"Expected slurmctld to reject the non-TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (26, 5, 4), reason=REASON)
def test_slurmctld_accepts_tls_without_http(slurmctld_target, probe):
    """Control: the same probe under the real (TLS) config is accepted."""
    result = atf.run_command(
        f"{probe} {slurmctld_target}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=ACCEPTED" in output
    ), f"Expected slurmctld to accept the valid TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmd") < (26, 5, 4), reason=REASON)
def test_slurmd_rejects_non_tls_without_http(slurmd_target, non_tls_conf, probe):
    """A non-TLS client is rejected by slurmd with HTTP listening disabled."""
    result = atf.run_command(
        f"{probe} {slurmd_target}",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in output
    ), f"Expected slurmd to reject the non-TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmd") < (26, 5, 4), reason=REASON)
def test_slurmd_accepts_tls_without_http(slurmd_target, probe):
    """Control: the same probe under the real (TLS) config reaches slurmd.

    slurmd marks REQUEST_PING from_slurmctld, so what a dispatched ping answers
    depends on who runs the probe: an unprivileged sender is refused
    ESLURM_USER_ID_MISSING at the authorization check, while root or SlurmUser
    reaches _rpc_ping() and gets RESPONSE_PING_SLURMD, which
    slurm_get_return_code() maps to SLURM_SUCCESS. Accept either; both are only
    reachable once the listener has accepted the connection and dispatched the
    RPC, which is all this control needs to show.
    """
    result = atf.run_command(
        f"{probe} {slurmd_target}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=ACCEPTED" in output or f"rc={ESLURM_USER_ID_MISSING}" in output
    ), f"Expected slurmd to accept and dispatch the TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (25, 11), reason=REASON)
def test_plaintext_http_not_served(slurmctld_target):
    """No plaintext HTTP is served once disable_http is set.

    tls.shtml tells admins to set disable_http if the port must not carry
    cleartext at all, so pin that it does what it is recommended for.
    test_166_1 pins the inverse, that the port serves HTTP without it.
    """
    host, port = slurmctld_target.split()

    with pytest.raises(requests.exceptions.RequestException):
        resp = requests.get(f"http://{host}:{port}/")
        pytest.fail(
            f"Expected no plaintext HTTP with disable_http, got"
            f" {resp.status_code}:\n{resp.text}"
        )
