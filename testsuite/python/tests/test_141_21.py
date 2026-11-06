############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that scontrol power up/down reports the nodes it skips.

Ticket 24819: 'scontrol power up' on nodes that are not POWERED_DOWN (e.g. DOWN
nodes) and 'scontrol power down' on nodes that are already powering down
report the skipped nodes through err_msg, the same way 'scontrol reboot' does.
The exit code stays 0 and eligible nodes in the same request are still
powered up/down.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

# The test never wants the automatic power save to suspend nodes on its own:
# nodes start powered down via State=CLOUD and node2 is kept plain DOWN by an
# admin reason, so a big SuspendTime keeps the scheduler out of the way.
suspend_time = 3600
# Nodes powered up during the test stay POWERING_UP (ResumeProgram=/bin/true
# never registers a slurmd) and must not flip to DOWN+POWERED_DOWN via
# ResumeTimeout in the middle of the test.
resume_timeout = 300
# Nodes powered down during the test must stay POWERING_DOWN and not flip to
# POWERED_DOWN via SuspendTimeout in the middle of the test.
suspend_timeout = 300

skip_msg = "Skipping power up of nodes"
skip_down_msg = "Skipping power down of nodes"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmctld only sends the message to clients of 26.11 or later, and
    # slurmctld is never older than scontrol
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Ticket 24819: power up reports skipped nodes",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)

    atf.require_config_parameter(
        "NodeName",
        {
            "node1": {"State": "CLOUD"},
            "node2": {"State": "CLOUD", "Port": atf.get_open_port()},
            "node3": {"State": "CLOUD"},
            "node4": {"State": "CLOUD", "Port": atf.get_open_port()},
        },
    )
    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": "ALL", "Default": "YES"},
        },
    )
    # Don't run the usual atf.require_slurm_running() because the test starts
    # the slurmd for node2 manually
    atf.start_slurmctld(clean=True)

    yield

    # conftest doesn't stop daemons it didn't start, so stop them here. This
    # also covers the manually started node2 slurmd, which is not one of the
    # slurmds conftest knows about.
    atf.stop_slurmctld(also_slurmds=True)


def skip_up(nodes):
    return f"{skip_msg} {nodes} due to current node state."


def skip_down(nodes):
    return f"{skip_down_msg} {nodes} due to current node state."


def powering_up(node):
    # update_node() sets POWER_UP during the RPC, and the power save thread
    # only adds POWERING_UP up to a second later, so check for either
    return {"POWER_UP", "POWERING_UP"} & set(
        atf.get_node_parameter(node, "state", fatal=True)
    )


def test_power_up_reports_skipped_nodes():
    slurm_user = atf.properties["slurm-user"]

    # All cloud nodes start powered down
    for node in ["node1", "node2", "node3", "node4"]:
        atf.wait_for_node_state(node, "POWERED_DOWN", fatal=True)

    # Baseline: powering up a POWERED_DOWN node is accepted silently
    results = atf.run_command("scontrol power up node3", user=slurm_user)
    assert results["exit_code"] == 0, f"power up node3 failed: {results['stderr']}"
    assert (
        skip_msg not in results["stderr"]
    ), f"POWERED_DOWN node must not be reported as skipped: {results['stderr']}"
    atf.wait_for_node_state("node3", "POWERING_UP", fatal=True)

    # Register a slurmd for node2 so it leaves POWERED_DOWN, then set it DOWN
    # with an admin reason to emulate a node that died outside of power save
    # (e.g. 'shutdown -h'), which is the scenario from Ticket 24819
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N node2",
        fatal=True,
        user="root",
    )
    atf.wait_for_node_state("node2", "IDLE", fatal=True)
    # Polling for IDLE intersects IDLE+POWERED_DOWN, which a CLOUD node keeps
    # until its registration completes, so explicitly wait for registration
    # to clear POWERED_DOWN before setting the node DOWN
    atf.wait_for_node_state("node2", "POWERED_DOWN", reverse=True, fatal=True)
    atf.run_command(
        "scontrol update nodename=node2 state=down reason=t24819",
        user=slurm_user,
        fatal=True,
    )
    atf.wait_for_node_state("node2", "DOWN", fatal=True)
    assert "POWERED_DOWN" not in atf.get_node_parameter(
        "node2", "state", fatal=True
    ), "node2 must not be POWERED_DOWN to reproduce the ticket scenario"

    # The ticket case: power up of a DOWN node is rejected with a message
    results = atf.run_command("scontrol power up node2", user=slurm_user)
    assert results["exit_code"] == 0, f"power up node2 failed: {results['stderr']}"
    assert (
        skip_up("node2") in results["stderr"]
    ), f"Skipped node2 must be reported to the user: {results['stderr']}"
    assert not powering_up("node2"), "node2 must not be powered up without force"

    # The obsoleted 'update state=power_up' interface goes through the same
    # RPC and must report the skip as well
    results = atf.run_command(
        "scontrol update nodename=node2 state=power_up", user=slurm_user
    )
    assert (
        results["exit_code"] == 0
    ), f"update state=power_up failed: {results['stderr']}"
    assert (
        skip_up("node2") in results["stderr"]
    ), f"Skipped node2 must be reported to the user: {results['stderr']}"
    assert not powering_up("node2"), "node2 must not be powered up without force"

    # Mixed request: the eligible node is powered up and only the skipped one
    # is reported. The message lists nodes as a hostlist range, so compare the
    # whole message: a wrongly reported node1 would show up as node[1-2].
    results = atf.run_command("scontrol power up node1,node2", user=slurm_user)
    assert (
        results["exit_code"] == 0
    ), f"power up node1,node2 failed: {results['stderr']}"
    assert (
        skip_up("node2") in results["stderr"]
    ), f"Skipped node2 must be reported to the user: {results['stderr']}"
    atf.wait_for_node_state("node1", "POWERING_UP", fatal=True)
    assert not powering_up("node2"), "node2 must not be powered up without force"

    # 'power up force' still overrides the state check without any message
    results = atf.run_command("scontrol power up force node2", user=slurm_user)
    assert (
        results["exit_code"] == 0
    ), f"power up force node2 failed: {results['stderr']}"
    assert (
        skip_msg not in results["stderr"]
    ), f"'power up force' must not report skipped nodes: {results['stderr']}"
    atf.wait_for_node_state("node2", "POWERING_UP", fatal=True)

    # Register a slurmd for node4 so it can be powered down. A repeated
    # 'power down' of a node that is already powering down is rejected with
    # a message, and so is a conflicting 'power up'
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N node4",
        fatal=True,
        user="root",
    )
    atf.wait_for_node_state("node4", "IDLE", fatal=True)
    atf.wait_for_node_state("node4", "POWERED_DOWN", reverse=True, fatal=True)

    # Power up of a node that is already powered up is skipped too
    results = atf.run_command("scontrol power up node4", user=slurm_user)
    assert results["exit_code"] == 0, f"power up node4 failed: {results['stderr']}"
    assert (
        skip_up("node4") in results["stderr"]
    ), f"Skipped node4 must be reported to the user: {results['stderr']}"
    assert not powering_up("node4"), "node4 must not be powered up again"

    # Mixed power down: node2, still powering up after the forced power up,
    # is first sent powering down. Powering down node2 again together with
    # node4 then skips node2 and still powers down the eligible node4.
    results = atf.run_command("scontrol power down node2", user=slurm_user)
    assert results["exit_code"] == 0, f"power down node2 failed: {results['stderr']}"
    assert (
        skip_down_msg not in results["stderr"]
    ), f"node2 must not be reported as skipped: {results['stderr']}"
    atf.wait_for_node_state("node2", "POWERING_DOWN", fatal=True)

    results = atf.run_command("scontrol power down node2,node4", user=slurm_user)
    assert (
        results["exit_code"] == 0
    ), f"power down node2,node4 failed: {results['stderr']}"
    assert (
        skip_down("node2") in results["stderr"]
    ), f"Skipped node2 must be reported to the user: {results['stderr']}"
    atf.wait_for_node_state("node4", "POWERING_DOWN", fatal=True)

    results = atf.run_command("scontrol power down node4", user=slurm_user)
    assert (
        results["exit_code"] == 0
    ), f"repeated power down node4 failed: {results['stderr']}"
    assert (
        skip_down("node4") in results["stderr"]
    ), f"Skipped node4 must be reported to the user: {results['stderr']}"

    # Neither asap nor force overrides the check for a node already powering
    # down
    for option in ["asap", "force"]:
        results = atf.run_command(
            f"scontrol power down {option} node4", user=slurm_user
        )
        assert (
            results["exit_code"] == 0
        ), f"power down {option} node4 failed: {results['stderr']}"
        assert (
            skip_down("node4") in results["stderr"]
        ), f"Skipped node4 must be reported to the user: {results['stderr']}"

    results = atf.run_command("scontrol power up node4", user=slurm_user)
    assert results["exit_code"] == 0, f"power up node4 failed: {results['stderr']}"
    assert (
        skip_up("node4") in results["stderr"]
    ), f"Skipped node4 must be reported to the user: {results['stderr']}"
