############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import pytest

import atf

FEATURE = "joinfa"
NODESET = "ns_joinfa"
PARTITION = "featpart"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Needs to define a Nodeset and feature-based partition")
    atf.require_nodes(2)
    atf.require_config_parameter("Nodeset", {NODESET: {"Feature": FEATURE}})
    atf.require_config_parameter("PartitionName", {PARTITION: {"Nodes": NODESET}})
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def restore_features():
    saved = {
        n: (
            atf.get_node_parameter(n, "features", default=[]),
            atf.get_node_parameter(n, "active_features", default=[]),
        )
        for n in atf.nodes
    }
    yield
    for node, (avail, active) in saved.items():
        avail_str = ",".join(avail) if avail else ""
        active_str = ",".join(active) if active else ""
        atf.run_command(
            f"scontrol update nodename={node} "
            f"activefeatures={active_str} availablefeatures={avail_str}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )


def _nodes_in_partition(partition):
    """Returns the set of nodes currently assigned to the given partition."""
    output = atf.run_command_output(
        f"sinfo -h -p {partition} -o %N", fatal=True
    ).strip()
    return set(atf.node_range_to_list(output)) if output else set()


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 50815: nodes did not join feature-based NodeSet partitions "
    "after scontrol update before 26.11.",
)
def test_node_joins_partition_on_feature_update():
    """A node gains membership in a NodeSet-backed partition immediately after
    scontrol update sets the required feature."""
    node = sorted(atf.nodes)[0]

    assert node not in _nodes_in_partition(PARTITION), (
        f"Precondition: node {node} should not be in partition {PARTITION} "
        f"before its features are updated"
    )

    atf.run_command(
        f"scontrol update nodename={node} availablefeatures={FEATURE}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert node in _nodes_in_partition(PARTITION), (
        f"Node {node} should be in partition {PARTITION} after "
        f"AvailableFeatures was updated to include '{FEATURE}'"
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 50815: nodes did not join feature-based NodeSet partitions "
    "after scontrol update before 26.11.",
)
def test_node_leaves_partition_on_feature_clear():
    """A node loses membership in a NodeSet-backed partition immediately after
    scontrol update clears the required feature."""
    node = sorted(atf.nodes)[0]

    atf.run_command(
        f"scontrol update nodename={node} availablefeatures={FEATURE}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert node in _nodes_in_partition(PARTITION), (
        f"Precondition: node {node} should be in partition {PARTITION} "
        f"after AvailableFeatures was set to '{FEATURE}'"
    )

    atf.run_command(
        f"scontrol update nodename={node} activefeatures= availablefeatures=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert node not in _nodes_in_partition(PARTITION), (
        f"Node {node} should not be in partition {PARTITION} after "
        f"AvailableFeatures was cleared"
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 50815: nodes did not join feature-based NodeSet partitions "
    "after scontrol update before 26.11.",
)
def test_unaffected_node_does_not_join_partition():
    """Updating one node's features must not pull a different node into the
    partition."""
    target, other = sorted(atf.nodes)[:2]

    members = _nodes_in_partition(PARTITION)
    assert target not in members, (
        f"Precondition: node {target} should not be in partition {PARTITION} "
        f"before its features are updated"
    )
    assert other not in members, (
        f"Precondition: node {other} should not be in partition {PARTITION} "
        f"before any features are updated"
    )

    atf.run_command(
        f"scontrol update nodename={target} availablefeatures={FEATURE}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    members = _nodes_in_partition(PARTITION)
    assert target in members, (
        f"Node {target} should be in partition {PARTITION} after its "
        f"AvailableFeatures was set to '{FEATURE}'"
    )
    assert other not in members, (
        f"Node {other} should not be in partition {PARTITION} — its "
        f"AvailableFeatures was not changed"
    )
