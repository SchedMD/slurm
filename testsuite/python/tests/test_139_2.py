############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Regression: an EXTERNAL node must not enter NOT_RESPONDING after resume.

EXTERNAL nodes have no slurmd, so the controller must not flag them as
NODE_STATE_NO_RESPOND when transitioning back to IDLE via `state=resume`.
"""
import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Wants to create EXTERNAL dynamic nodes")
    atf.require_config_parameter("MaxNodeCount", 8)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_slurm_running()


@pytest.fixture
def external_node():
    """Create an EXTERNAL node and delete it after the test."""
    node_name = "ext_resume_test"
    slurm_user = atf.properties["slurm-user"]

    atf.run_command(
        f"scontrol create nodename={node_name} state=external",
        user=slurm_user,
        fatal=True,
    )

    yield node_name

    atf.run_command(
        f"scontrol delete nodename={node_name}",
        user=slurm_user,
        quiet=True,
    )


def _states(node):
    st = atf.get_node_parameter(node, "state")
    if isinstance(st, str):
        return [st]
    return st


@pytest.mark.xfail(
    atf.get_version("bin/scontrol") < (25, 11),
    reason="Ticket 50831: EXTERNAL node resume fix is only present in slurmctld 25.11+, but scontrol required due issue #50689 of NODE_STATE_EXTERNAL missing with --json",
)
@pytest.mark.parametrize("pre_state", ["drain", "down"])
def test_resume_external_no_not_responding(external_node, pre_state):
    """resume on an EXTERNAL node must clear/avoid NOT_RESPONDING."""
    node = external_node
    slurm_user = atf.properties["slurm-user"]

    initial = _states(node)
    assert "EXTERNAL" in initial, f"Setup error: {node} not EXTERNAL: {initial!r}"
    assert (
        "NOT_RESPONDING" not in initial
    ), f"Fresh EXTERNAL {node} should not be NOT_RESPONDING: {initial!r}"

    atf.run_command(
        f"scontrol update nodename={node} state={pre_state} reason=test_external_resume",
        user=slurm_user,
        fatal=True,
    )

    expected_pre = "DRAIN" if pre_state == "drain" else "DOWN"
    st = _states(node)
    assert expected_pre in st, (
        f"EXTERNAL {node} should be {expected_pre} after state={pre_state}; "
        f"state={st!r}"
    )
    assert "NOT_RESPONDING" not in st, (
        f"EXTERNAL {node} must not be NOT_RESPONDING after state={pre_state}; "
        f"state={st!r}"
    )

    atf.run_command(
        f"scontrol update nodename={node} state=resume",
        user=slurm_user,
        fatal=True,
    )

    st = _states(node)
    assert "IDLE" in st, (
        f"EXTERNAL {node} should be IDLE after resume from {pre_state}; "
        f"state={st!r}"
    )
    assert expected_pre not in st, (
        f"EXTERNAL {node} should no longer be {expected_pre} after resume; "
        f"state={st!r}"
    )
    assert "NOT_RESPONDING" not in st, (
        f"EXTERNAL {node} must not be NOT_RESPONDING after resume from "
        f"{pre_state}; state={st!r}"
    )
