############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify topology/tree + RouteTree relays a forwarded message once per switch.

Even when the target hostlist spans top-level switches that share no common
root (a "forest" of disconnected trees), every top-level switch that no parent
groups into a forward sublist - whether a bare leaf or the root of its own
tree - must get its own relay sublist instead of dropping every node to the
per-node fallback.

A full-cluster REQUEST_PING uses the forwarding tree, so its controller-side
split is observable in the slurmctld ROUTE debug log:
    RELAY    : "] switch=<name>"              -> one relay sublist per switch
    FALLBACK : "find switch containing nodes" -> per-node fan-out (the bug)
The split decision is the only observable: fan-out and per-node fan-out both
deliver the ping, so there is no wire-level outcome to assert on. Each case
installs its own topology and truncates the log first, so its assertions see
only that split.
"""

import logging
import re

import pytest

import atf

# Two leaf switches with no common root.
DISCONNECTED_LEAVES = """\
- topology: poc
  cluster_default: true
  tree:
    switches:
      - switch: sw_a
        nodes: node[1-16]
      - switch: sw_b
        nodes: node[17-32]
"""

# Two multi-leaf trees (trunks) with no common root. Each trunk collapses to
# one relay per child leaf - the path that always worked - but the trunks
# share no root, so neither is reachable from the other.
DISCONNECTED_TRUNKS = """\
- topology: poc
  cluster_default: true
  tree:
    switches:
      - switch: trunk_a
        children: leaf_a1,leaf_a2
      - switch: trunk_b
        children: leaf_b1,leaf_b2
      - switch: leaf_a1
        nodes: node[1-8]
      - switch: leaf_a2
        nodes: node[9-16]
      - switch: leaf_b1
        nodes: node[17-24]
      - switch: leaf_b2
        nodes: node[25-32]
"""

# One trunk and a standalone leaf with no common root: the trunk relays per
# child switch (always worked) and the bare leaf relays its own nodes (the
# fix), both in the same split.
MIXED = """\
- topology: poc
  cluster_default: true
  tree:
    switches:
      - switch: sw_root1
        children: sw_a,sw_b
      - switch: sw_a
        nodes: node[1-8]
      - switch: sw_b
        nodes: node[9-16]
      - switch: sw_c
        nodes: node[17-32]
"""

# All leaves under a single root: connected control, no disconnection.
ROOTED = """\
- topology: poc
  cluster_default: true
  tree:
    switches:
      - switch: sw_root
        children: sw_a,sw_b
      - switch: sw_a
        nodes: node[1-16]
      - switch: sw_b
        nodes: node[17-32]
"""

FALLBACK = "find switch containing nodes"  # per-node fallback (the bug)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmctld",
        reason="Ticket 25473: leaf-relay fan-out fix in 26.05+",
    )
    atf.require_version(
        (25, 5), "bin/scontrol", reason="The topology.yaml option was added in 25.05"
    )
    atf.require_nodes(32)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("TopologyPlugin", "topology/tree")
    atf.require_config_parameter_includes("TopologyParam", "RouteTree")
    atf.require_config_parameter("SlurmctldDebug", "debug")
    atf.require_config_parameter_includes("DebugFlags", "Route")
    # Ping cadence is SlurmdTimeout/3, so a full-cluster REQUEST_PING fires
    # every few seconds and logs its topology split.
    atf.require_config_parameter("SlurmdTimeout", "15")
    atf.require_config_file("topology.yaml", DISCONNECTED_LEAVES)
    atf.require_slurm_running()


def _logfile():
    return atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)


def _count(pattern):
    out = atf.run_command_output(
        f'grep -c -F -- "{pattern}" {_logfile()} || true',
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    return int((out or "0").strip() or 0)


def _relayed_switches():
    """Set of switch names that received a relay sublist, parsed from the
    per-switch ROUTE split lines (`] switch=<name> ::`)."""
    out = atf.run_command_output(
        f"grep -oE '] switch=[^ ]+' {_logfile()} || true",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    return {m.group(1) for m in re.finditer(r"] switch=(\S+)", out or "")}


@pytest.fixture
def apply_topology(request):
    """Install the parametrized topology.yaml, reconfigure, and truncate the
    log so the test sees only this topology's ROUTE split."""
    atf.require_config_file("topology.yaml", request.param)
    atf.run_command(
        "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
    )
    atf.run_command(
        f"truncate -s 0 {_logfile()}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


@pytest.mark.parametrize(
    "apply_topology, expected",
    [
        pytest.param(DISCONNECTED_LEAVES, {"sw_a", "sw_b"}, id="disconnected_leaves"),
        pytest.param(
            DISCONNECTED_TRUNKS,
            {"leaf_a1", "leaf_a2", "leaf_b1", "leaf_b2"},
            id="disconnected_trunks",
        ),
        pytest.param(
            MIXED, {"sw_a", "sw_b", "sw_c"}, id="mixed_trunk_and_standalone_leaf"
        ),
        pytest.param(ROOTED, {"sw_a", "sw_b"}, id="rooted_tree"),
    ],
    indirect=["apply_topology"],
)
def test_route_tree_fanout(apply_topology, expected):
    """Each top-level switch relays its own sublist and the per-node fallback
    never fires, so the message fans out once per switch."""
    relayed = set()
    for _t in atf.timer():
        relayed = _relayed_switches()
        if expected <= relayed:
            break
    else:
        pytest.fail(
            f"expected one relay sublist per switch {sorted(expected)}; "
            f"saw relays for {sorted(relayed)}"
        )
    logging.info("relayed switches: %s", sorted(relayed))
    assert (
        expected == relayed
    ), f"unexpected relay sublists: expected {sorted(expected)}, saw {sorted(relayed)}"
    assert (
        _count(FALLBACK) == 0
    ), f"per-node fallback ('{FALLBACK}') fired; expected per-switch relay"
