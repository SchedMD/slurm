############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that renaming a leaf switch drains the nodes that referenced it.

This is the topology/tree counterpart of test_150_23. The drain itself is
plugin agnostic, but the name resolution that fails first is the plugin's, so
both plugins need coverage.

A node whose topology names a single leaf switch fails to resolve once that
switch is renamed. A multi-level unit like "sw_root:sw_new" behaves
differently, because the tree plugin creates a missing child under a parent it
does know, so this test only covers the bare leaf form.
"""

import re

import pytest

import atf

# test_150_5 owns 61500-61519 and test_150_23 owns 61520-61523.
PORT_BASE = 61530

NODES = [
    {"name": "node1", "topo": "tree_topo:sw_alpha", "port": PORT_BASE},
    {"name": "node2", "topo": "tree_topo:sw_plain", "port": PORT_BASE + 1},
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        reason="Issue 50932: draining a node whose topology string names a missing switch was added in 26.11",
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter_includes("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("MaxNodeCount", 4)
    atf.require_config_file("topology.yaml", topology_yaml("sw_alpha"))

    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def dynamic_nodes(setup):
    for node in NODES:
        start_dynamic_slurmd(node["name"], node["port"], node["topo"])

    yield

    for node in NODES:
        # A live slurmd would re-register and resurrect the deleted record.
        atf.stop_slurmd(node["name"])
        atf.delete_node(node["name"])


def topology_yaml(leaf_name):
    """Return a topology.yaml whose first leaf switch carries the given name."""
    return f"""
- topology: tree_topo
  cluster_default: true
  tree:
    switches:
      - switch: sw_root
        children: {leaf_name},sw_plain
      - switch: {leaf_name}
      - switch: sw_plain
"""


def start_dynamic_slurmd(name, port, topology):
    """Start a dynamic-normal slurmd and wait for it to register as IDLE."""
    atf.start_slurmd(name, f"-Z --conf 'Port={port} topology={topology}'", wait="idle")


def reconfigure():
    """Reconfigure slurmctld and assert that it survived.

    No wait is needed afterwards. A reconfigure re-execs slurmctld, and the
    old process only answers this scontrol once the new one has finished
    read_slurm_conf(), so every node whose topology string stopped resolving
    is already drained by the time this returns.
    """
    assert (
        atf.run_command_exit("scontrol reconfigure", user=atf.properties["slurm-user"])
        == 0
    ), "slurmctld should survive the reconfigure"


def switches_for_node(name, topology):
    """Return the set of leaf switches in a topology that list the node.

    scontrol shows only the cluster default topology when given no name, so
    the topology has to be named. Only level 0 switches list nodes, so
    intermediate ones are ignored.
    """
    switches = set()
    output = atf.run_command_output(f"scontrol show topology {topology}", fatal=True)
    # scontrol only writes to stderr and still exits 0 when it cannot load a
    # topology, so fatal=True does not catch that and an empty output would
    # make an "is in no switch" assertion pass vacuously.
    assert output != "", f"scontrol should be able to show the {topology} topology"
    for line in output.splitlines():
        match = re.match(r"SwitchName=(\S+) Level=0 .*Nodes=(\S+)", line)
        if match and name in atf.node_range_to_list(match.group(2)):
            switches.add(match.group(1))
    return switches


def update_node(name, parameters):
    """Run scontrol update on a node and return the command result."""
    return atf.run_command(
        f"scontrol update nodename={name} {parameters}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture(scope="module", autouse=True)
def renamed_switch(dynamic_nodes):
    """Rename sw_alpha so every test starts with node1 drained."""
    atf.require_config_file("topology.yaml", topology_yaml("sw_delta"))
    reconfigure()
    assert atf.wait_for_node_state(
        "node1", "DRAIN"
    ), "node1 referenced the renamed switch and should be drained"


def test_switch_rename_drains_nodes():
    """Renaming a leaf switch drains only the nodes that referenced it."""

    nodes = atf.get_nodes()
    assert "DRAIN" in nodes["node1"]["state"], "node1 should be drained"
    assert (
        nodes["node1"]["reason"] == "InvalidTopology"
    ), "node1 should say why it was drained"

    # sw_plain was not renamed, so node2 must be left alone.
    assert (
        "DRAIN" not in nodes["node2"]["state"]
    ), "node2 still has a valid topology and should not be drained"


def test_recover_with_valid_switch():
    """Pointing the node at the renamed switch recovers it."""

    assert (
        update_node("node1", "topology=tree_topo:sw_delta")["exit_code"] == 0
    ), "node1 should accept a topology that names an existing switch"

    nodes = atf.get_nodes()
    assert (
        nodes["node1"]["topology"] == "tree_topo:sw_delta"
    ), "node1 should now be affiliated with sw_delta"
    # The string above is echoed back verbatim, so check real placement.
    assert switches_for_node("node1", "tree_topo") == {
        "sw_delta"
    }, "node1 should actually be placed under sw_delta"

    # Fixing the topology does not undrain the node.
    assert "DRAIN" in nodes["node1"]["state"], "node1 should stay drained"
    assert (
        nodes["node1"]["reason"] == "InvalidTopology"
    ), "node1 should keep the reason it was drained for"

    assert (
        update_node("node1", "state=resume")["exit_code"] == 0
    ), "node1 should now resume"

    nodes = atf.get_nodes()
    assert "DRAIN" not in nodes["node1"]["state"], "node1 should no longer be drained"
