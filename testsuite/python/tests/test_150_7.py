############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a slurm.conf per-node topology survives a config record split.

When several nodes are defined on one NodeName= line they share a single
config_record_t. Updating only a subset of those nodes (Weight=, Features=
or Gres=) splits the record via _dup_config() in src/slurmctld/node_mgr.c.
The duplicated record must carry topology_str; otherwise the split-off node
loses its configured topology.

The loss is not visible from `scontrol show node` immediately after the
split (the node record keeps its own topology_str copy), but it surfaces the
next time the node record is rebuilt from its config record. reset_node_topology()
does exactly that when a node finishes powering down (power_save.c), reading
node_ptr->config_ptr->topology_str as the value to restore. topology_orig_str
is only populated when the topology is NOT configured in slurm.conf (see
_restore_node_topology_str() in slurmctld/read_config.c), so with the topology
in slurm.conf a dropped config topology_str leaves nothing to fall back on and
the node's topology is cleared.

So the sequence is:
    NodeName=node[1-2] ... Topology=tree_topo:sw_plain  (shared config)
    scontrol update NodeName=node1 Weight=<new>         (splits the config)
    scontrol update NodeName=node1 State=POWER_DOWN_FORCE (reset_node_topology)
Before the fix node1's topology is cleared; after the fix it is preserved.
"""

import re

import pytest

import atf

TOPO = "tree_topo:sw_plain"
NEW_WEIGHT = 5


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 51012: splitting a node config dropped topology_str before 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")

    # Power save must be enabled for reset_node_topology() to run on power
    # down. SuspendTime=INFINITE keeps nodes from auto-suspending so the test
    # controls the power-down explicitly; a short SuspendTimeout keeps the
    # POWERED_DOWN transition quick.
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", "INFINITE")
    atf.require_config_parameter("SuspendTimeout", 5)
    atf.require_config_parameter("ResumeTimeout", 30)
    atf.require_config_parameter_includes("SlurmctldParameters", "idle_on_node_suspend")

    # A tree topology with a named leaf switch the nodes are pinned under, so
    # the per-node Topology= string is meaningful and we can verify placement
    # via `scontrol show topology`. The tree_topo: prefix is how the
    # multi-topology dispatcher reads node_ptr->topology_str.
    atf.require_config_file(
        "topology.yaml",
        """
- topology: tree_topo
  cluster_default: true
  tree:
    switches:
      - switch: sw_root
        children: sw_plain,sw_other
      - switch: sw_plain
      - switch: sw_other
""",
    )

    # Two nodes with identical config (including the slurm.conf topology and a
    # non-default CpuBind) so they consolidate into a single config_record_t at
    # startup (see consolidate_config_list() in read_slurm_conf()). CpuBind is
    # one of the fields _dup_config() must copy onto the split-off record.
    # require_nodes() assigns them distinct ports so both slurmds register
    # under multi-slurmd.
    atf.require_nodes(
        2, [("CPUs", 1), ("RealMemory", 64), ("Topology", TOPO), ("CpuBind", "core")]
    )

    atf.require_slurm_running()


def _node_topology(name):
    """Return the node's currently reported Topology= string, or None."""
    return atf.get_node_parameter(name, "topology")


def _switch_for_node(name):
    """Return the leaf SwitchName containing `name`, or None.

    Only level-0 (leaf) switches list nodes, so intermediate switches are
    ignored."""
    out = atf.run_command_output("scontrol show topology", fatal=False)
    for line in out.splitlines():
        m = re.match(r"SwitchName=(\S+) Level=0 .*Nodes=(\S+)", line)
        if not m:
            continue
        sw, nodes_expr = m.group(1), m.group(2)
        if name in atf.node_range_to_list(nodes_expr):
            return sw
    return None


def test_topology_preserved_when_config_split_by_partial_update():
    """A partial Weight= update splits the shared config record; the split-off
    node must keep its slurm.conf topology across a power-down cycle."""

    # Derive the two nodes require_nodes() created rather than assuming the
    # node1/node2 naming of a node0-template reference config: `victim` is
    # updated (splitting its config record) and `sharer` stays untouched.
    victim, sharer = sorted(atf.get_nodes(quiet=True))[:2]

    # Precondition: both nodes share the configured topology.
    assert (
        _node_topology(victim) == TOPO
    ), f"{victim} should start with configured topology"
    assert (
        _node_topology(sharer) == TOPO
    ), f"{sharer} should start with configured topology"

    # Update only the victim's weight. Because it shares a config record with
    # the sharer, this splits the record for the victim via _dup_config().
    atf.run_command(
        f"scontrol update NodeName={victim} Weight={NEW_WEIGHT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        if str(atf.get_node_parameter(victim, "weight")) == str(NEW_WEIGHT):
            break
    # The sharer must be unaffected by the split.
    assert str(atf.get_node_parameter(sharer, "weight")) != str(
        NEW_WEIGHT
    ), f"{sharer} should keep its original weight after the split"

    # The split must not reset other shared config fields on the victim. CpuBind
    # is copied by _dup_config() alongside topology_str; the victim must still
    # match the untouched sharer.
    assert atf.get_node_parameter(victim, "cpubind") == atf.get_node_parameter(
        sharer, "cpubind"
    ), f"{victim} should keep its configured CpuBind after the config split"

    # Force the victim to power down. On the POWERED_DOWN transition the
    # controller calls reset_node_topology(), which rebuilds the victim's
    # topology from its (split-off) config record.
    atf.run_command(
        f"scontrol update NodeName={victim} State=POWER_DOWN_FORCE",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(victim, "POWERED_DOWN", fatal=True)

    # The split-off config record must still carry the topology, so the victim's
    # topology is restored (not cleared) after the reset.
    for _ in atf.timer():
        if _node_topology(victim) == TOPO:
            break
    else:
        pytest.fail(
            f"{victim} topology should be preserved after config split, got {_node_topology(victim)!r}"
        )

    # And it should still be placed under the configured leaf switch.
    for _ in atf.timer():
        if _switch_for_node(victim) == "sw_plain":
            break
    else:
        pytest.fail(
            f"{victim} should remain under sw_plain, got {_switch_for_node(victim)!r}"
        )
