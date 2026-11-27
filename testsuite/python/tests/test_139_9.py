############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a recreated dynamic node is not marked as rebooted.

A reply from a deleted node's slurmd must not be applied to a new node with
the same name. If it were, the new node would be set DOWN with "Node
unexpectedly rebooted" when it first registers with slurmd -b.
"""

import time

import pytest

import atf

# slurmctld may apply a reply up to this long after it arrives
reply_delay = 2

# The node is recreated milliseconds after the job ends, so the job's reply is
# almost always applied to the new node. Repeat in case it is not.
cycles = 3
node_name = "reusednode"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MaxNodeCount", 5)
    atf.require_config_parameter("PartitionName", {"primary": {"Nodes": "ALL"}})

    # Needed for MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Needed for cons_tres
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    # A node is never marked "unexpectedly rebooted" with ReturnToService=2
    atf.require_config_parameter("ReturnToService", 1)

    atf.require_slurm_running()


@pytest.fixture
def cleanup_node():
    yield

    atf.stop_slurmd(node_name)
    if node_name in atf.get_nodes(live=True, quiet=True, fatal=True):
        atf.run_command(
            f"scontrol delete NodeName={node_name}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


@pytest.mark.parametrize(
    "slurmd_args",
    [
        pytest.param(
            "-b",
            marks=pytest.mark.xfail(
                atf.get_version("sbin/slurmctld") < (26, 11, 1),
                reason="Issue 51075: A reply from the deleted node was still applied before 26.11.1.",
            ),
        ),
        # Without -b the reported uptime is the host's, so BootTime is far
        # behind any stale last response
        "",
    ],
    ids=["boot_flag", "no_boot_flag"],
)
def test_recreated_node_not_rebooted(cleanup_node, slurmd_args):
    """Test that a recreated node is not DOWN after its first registration"""

    atf.run_command(
        f"scontrol create NodeName={node_name} State=CLOUD",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.start_slurmd(node_name, f"-Z {slurmd_args}")

    for cycle in range(cycles):
        # Recreate the node as soon as the job ends, in one shell so the test
        # doesn't delay the recreate. A COMPLETING node can't be deleted, so
        # retry the delete.
        atf.run_command(
            f"""
            set -e
            srun -p primary -w {node_name} true
            for i in $(seq 100); do
                scontrol delete NodeName={node_name} 2>/dev/null && break
                sleep 0.05
            done
            scontrol create NodeName={node_name} State=CLOUD
            """,
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        # BootTime has 1 s resolution: register in a later second than any
        # stale reply. This is the condition under test, not an event wait.
        time.sleep(reply_delay + 1)
        atf.start_slurmd(node_name, f"-Z {slurmd_args}")

        node_states = atf.get_node_parameter(node_name, "state")
        reason = atf.get_node_parameter(node_name, "reason") or ""
        assert "DOWN" not in node_states, (
            f"Recreated node {node_name} should not be DOWN after its first"
            f" registration (cycle {cycle + 1} of {cycles}), got"
            f" {node_states} with reason '{reason}'"
        )
