############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test a node that belongs to several topologies losing one of them.

scontrol(1) documents Topology= as a comma separated list, so a node can be
affiliated with more than one topology at a time. Renaming a switch in one of
them makes the string as a whole unresolvable, so the node must drain.
Correcting only the broken half must be enough to let the node resume and
to put it back under both of its topologies.
"""

import re

import pytest

import atf

# test_150_5 owns 61500-61519, test_150_23 owns 61520-61523 and test_150_24
# owns 61530-61532.
PORT_BASE = 61540

NODES = [
    {"name": "node1", "topo": "topo_a:a_left,topo_b:b_left", "port": PORT_BASE},
    {"name": "node2", "topo": "topo_b:b_left", "port": PORT_BASE + 1},
    # topology_g_add_rm_node() stops at the first token that fails, so a
    # node whose broken half comes second takes a different path.
    {"name": "node3", "topo": "topo_b:b_left,topo_a:a_left", "port": PORT_BASE + 2},
]

# The topology each drained node is given to correct its renamed half.
CORRECTED = {
    "node1": "topo_a:a_delta,topo_b:b_left",
    "node3": "topo_b:b_left,topo_a:a_delta",
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        reason="Issue 50932: draining a node whose topology string names a missing switch was added in 26.11",
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("MaxNodeCount", 6)
    atf.require_config_file("topology.yaml", topology_yaml("a_left"))

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


def topology_yaml(left_switch_name):
    """Return a topology.yaml whose topo_a left switch carries the given name."""
    return f"""
- topology: topo_a
  cluster_default: true
  tree:
    switches:
      - switch: a_root
        children: {left_switch_name},a_right
      - switch: {left_switch_name}
      - switch: a_right
- topology: topo_b
  tree:
    switches:
      - switch: b_root
        children: b_left,b_right
      - switch: b_left
      - switch: b_right
"""


def start_dynamic_slurmd(name, port, topology):
    """Start a dynamic-normal slurmd and wait for it to register as IDLE."""
    atf.start_slurmd(name, f"-Z --conf 'Port={port} topology={topology}'", wait="idle")


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


def assert_in_no_topology(name):
    """Assert that no switch in either topology lists the node."""
    for topology in ("topo_a", "topo_b"):
        assert (
            switches_for_node(name, topology) == set()
        ), f"{name} should not be placed in any {topology} switch"


def update_node(name, parameters):
    """Run scontrol update on a node and return the command result."""
    return atf.run_command(
        f"scontrol update nodename={name} {parameters}",
        user=atf.properties["slurm-user"],
    )


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


@pytest.fixture(scope="module", autouse=True)
def renamed_switch(dynamic_nodes):
    """Rename the topo_a switch so node1's first affiliation stops resolving."""
    atf.require_config_file("topology.yaml", topology_yaml("a_delta"))
    reconfigure()
    assert atf.wait_for_node_state(
        ["node1", "node3"], "DRAIN"
    ), "Both nodes name the renamed switch and should be drained"


def test_one_invalid_affiliation_drains_the_node():
    """One unresolvable affiliation is enough to drain a multi-topology node."""

    nodes = atf.get_nodes()
    for name in ("node1", "node3"):
        assert "DRAIN" in nodes[name]["state"], f"{name} should be drained"
        assert (
            nodes[name]["reason"] == "InvalidTopology"
        ), f"{name} should say why it was drained"
        # Placement must not depend on where the broken affiliation sits in
        # the string, so the valid topo_b half is not applied either.
        assert_in_no_topology(name)

    # node2 is only in topo_b, which was not touched.
    assert (
        "DRAIN" not in nodes["node2"]["state"]
    ), "node2 still has a valid topology and should not be drained"


@pytest.mark.parametrize("name", list(CORRECTED))
def test_fixing_only_the_broken_affiliation_allows_resume(name):
    """Correcting the renamed half restores both affiliations.

    A correction that still does not resolve is refused first. The node must
    stay in no topology, although the refused string and node3's own string
    both apply a valid topo_b switch before they fail.
    """

    original = atf.get_nodes()[name]["topology"]
    assert (
        update_node(name, "topology=topo_b:b_right,topo_a:a_bogus")["exit_code"] != 0
    ), f"{name} should refuse a topology naming an unknown switch"
    assert (
        atf.get_nodes()[name]["topology"] == original
    ), f"{name} should keep its topology after a refused update"
    assert_in_no_topology(name)

    topology = CORRECTED[name]
    assert (
        update_node(name, f"topology={topology}")["exit_code"] == 0
    ), f"{name} should accept a topology naming the renamed switch"

    nodes = atf.get_nodes()
    assert (
        nodes[name]["topology"] == topology
    ), f"{name} should keep both topology affiliations"
    # The string above is echoed back verbatim, so check real placement too.
    assert switches_for_node(name, "topo_a") == {
        "a_delta"
    }, f"{name} should be placed under the renamed switch in topo_a"
    assert switches_for_node(name, "topo_b") == {
        "b_left"
    }, f"{name} should still be placed under its untouched topo_b switch"
    assert "DRAIN" in nodes[name]["state"], f"{name} should stay drained"
    assert (
        nodes[name]["reason"] == "InvalidTopology"
    ), f"{name} should keep the reason it was drained for"

    assert (
        update_node(name, "state=resume")["exit_code"] == 0
    ), f"{name} should now resume"

    nodes = atf.get_nodes()
    assert "DRAIN" not in nodes[name]["state"], f"{name} should no longer be drained"
