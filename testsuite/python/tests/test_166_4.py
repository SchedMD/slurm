############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify slurmdbd rejects clients that are not TLS wrapped.

Companion to test_166_1 and test_166_2, which cover the conmgr RPC
listeners. slurmdbd is fail-closed as well, but tls.shtml documents it as
reporting that differently: it negotiates TLS as soon as a connection is
accepted, so a client without TLS fails the negotiation and has its
connection closed, rather than being answered with ESLURM_TLS_REQUIRED.

That distinction is what these tests pin. Asserting only that the client
fails would pass even if slurmdbd had replied on an RPC listener, so the
reject leg also asserts the TLS-required error is absent.

slurmdbd takes its own TLSType from slurmdbd.conf, so it is required there
in addition to the cluster-wide setting the other modules need.
"""

import os

import pytest

import atf

# slurm_strerror(ESLURM_TLS_REQUIRED). What the RPC listeners answer and what
# slurmdbd, closing the connection instead, must not.
TLS_REQUIRED_MSG = "TLS missing but required for connection"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_tls()
    if not atf.get_config_parameter(
        "TLSType", default=None, live=False, source="slurmdbd", quiet=True
    ):
        pytest.skip(
            "Issue 51066: this test requires TLSType to be set in"
            " slurmdbd.conf as well as slurm.conf",
            allow_module_level=True,
        )
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def non_tls_conf(module_setup):
    """Path to a slurm.conf that includes the real config but disables TLS.

    Absolute because the tests consuming it run in their own directory. Only
    the process pointed at this via SLURM_CONF speaks plaintext; slurmdbd
    keeps the real configuration.
    """
    real_conf = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    path = "non_tls_slurm.conf"
    atf.run_command(
        f"printf 'include {real_conf}\\nTLSType=tls/none\\n' > {path}",
        fatal=True,
    )
    return os.path.abspath(path)


def test_slurmdbd_rejects_non_tls(non_tls_conf):
    """A non-TLS client cannot reach slurmdbd, and is not told why.

    sacctmgr takes its TLSType from slurm.conf, so pointing it at the
    non-TLS config makes it connect in plaintext. slurmdbd closes that
    connection during negotiation, so the failure carries no
    ESLURM_TLS_REQUIRED to explain itself.
    """
    result = atf.run_command(
        "sacctmgr -n show cluster",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        xfail=True,
    )

    assert (
        result["exit_code"] != 0
    ), f"Expected sacctmgr to fail without TLS, got:\n{result}"

    output = result["stdout"] + result["stderr"]
    assert TLS_REQUIRED_MSG not in output, (
        "Expected slurmdbd to close the connection without an"
        f" ESLURM_TLS_REQUIRED reply, got:\n{output}"
    )


def test_slurmdbd_accepts_tls():
    """Control: the same query under the real (TLS) config succeeds."""
    result = atf.run_command(
        "sacctmgr -n show cluster",
        fatal=True,
    )

    assert (
        result["exit_code"] == 0
    ), f"Expected sacctmgr to succeed with TLS, got:\n{result}"
