############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test renaming a block that slurm.conf nodes name in their Topology=.

test_150_23 to test_150_25 cover dynamic nodes. A node defined in slurm.conf
with its own Topology= must be drained the same way when the block it names is
renamed, and slurmctld must keep running.
"""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        reason="Issue 50932: draining a node whose topology string names a missing block was added in 26.11",
    )
    # The tests run scontrol, and a 25.05 scontrol (as in a mixed-version
    # upgrade) cannot print the node state transition error.
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Issue 50932: the feature is 26.11 only, so older scontrol is not exercised",
    )

    atf.require_nodes(
        2, [("CPUs", 1), ("RealMemory", 64), ("Topology", "default:blockA")]
    )
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    atf.require_config_file("topology.conf", topology_conf("blockA", "blockB"))

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes(setup):
    """Return the slurm.conf nodes that name blockA."""
    names = [
        name
        for name, node in atf.get_nodes().items()
        if node.get("topology") == "default:blockA"
    ]
    assert len(names) == 2, f"two nodes should name blockA, found {names}"
    return names


def topology_conf(*block_names):
    """Return a topology.conf defining the given blocks, in order."""
    lines = [f"BlockName={name}" for name in block_names]
    lines.append("BlockSizes=2")
    return "\n".join(lines) + "\n"


def reconfigure():
    """Reconfigure slurmctld and assert that it survived."""
    assert (
        atf.run_command_exit("scontrol reconfigure", user=atf.properties["slurm-user"])
        == 0
    ), "slurmctld should survive the reconfigure"


def update_node(name, parameters):
    """Run scontrol update on a node and return the command result."""
    return atf.run_command(
        f"scontrol update nodename={name} {parameters}",
        user=atf.properties["slurm-user"],
    )


def assert_resume_refused(name):
    """Assert that the node refuses State=RESUME for its node state."""
    result = update_node(name, "state=resume")
    assert result["exit_code"] != 0, f"{name} should refuse state=resume"
    assert (
        "Invalid node state transition" in result["stderr"]
    ), f"{name} should be refused for the node state, not another fault"


def test_block_rename_drains_nodes(nodes):
    """Renaming the block drains the nodes and refuses to resume them."""

    atf.require_config_file("topology.conf", topology_conf("blockZ", "blockB"))
    reconfigure()

    assert atf.wait_for_node_state(
        nodes, "DRAIN"
    ), "Nodes that name the renamed block should be drained"
    for name, node in atf.get_nodes().items():
        if name in nodes:
            assert (
                node["reason"] == "InvalidTopology"
            ), f"{name} should say why it was drained"

    assert_resume_refused(nodes[0])


def test_updated_topology_is_lost_on_reconfigure(nodes):
    """A topology set with scontrol update does not outlive a reconfigure.

    The node takes its Topology= from slurm.conf again, so a node resumed after
    an update is drained again while the block it names stays renamed.
    """

    name = nodes[0]
    atf.require_config_file("topology.conf", topology_conf("blockZ", "blockB"))
    reconfigure()
    assert (
        update_node(name, "topology=default:blockB")["exit_code"] == 0
    ), f"{name} should accept a topology that names blockB"
    assert (
        update_node(name, "state=resume")["exit_code"] == 0
    ), f"{name} should resume now that its topology resolves"
    assert (
        "DRAIN" not in atf.get_nodes()[name]["state"]
    ), f"{name} should no longer be drained"

    reconfigure()

    node = atf.get_nodes()[name]
    assert (
        node["topology"] == "default:blockA"
    ), f"{name} should take its topology from slurm.conf again"
    assert "DRAIN" in node["state"], f"{name} should be drained again"
    assert node["reason"] == "InvalidTopology", f"{name} should say why it was drained"
    assert_resume_refused(name)


def test_restoring_the_block_allows_resume(nodes):
    """Restoring the block lets the nodes resume, but does not undrain them."""

    atf.require_config_file("topology.conf", topology_conf("blockZ", "blockB"))
    reconfigure()
    atf.require_config_file("topology.conf", topology_conf("blockA", "blockB"))
    reconfigure()

    current = atf.get_nodes()
    for name in nodes:
        assert "DRAIN" in current[name]["state"], f"{name} should stay drained"
        assert (
            update_node(name, "state=resume")["exit_code"] == 0
        ), f"{name} should resume now that its topology resolves"

    current = atf.get_nodes()
    for name in nodes:
        assert (
            "DRAIN" not in current[name]["state"]
        ), f"{name} should no longer be drained"
