############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import re

import pytest

import atf

register_order = ["node102", "node12", "node101", "node11"]
alpha_order = ["node11", "node12", "node101", "node102"]
node_list_arg = ",".join(alpha_order)
nnodes = len(register_order)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="alphanumeric sort within a topology unit added in 26.11",
    )
    atf.require_version((26, 5), "sbin/slurmd")
    # Bootstrap with one static node; dynamic nodes register on top.
    atf.require_nodes(1)
    atf.require_config_parameter("MaxNodeCount", nnodes + 1)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    # Required for MaxNodeCount.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    # One tree topology (single switch) and one block topology (single
    # block); the dynamic nodes are placed in both at registration time.
    atf.require_config_file(
        "topology.yaml",
        """
- topology: tree_topo
  cluster_default: true
  tree:
    switches:
      - switch: sw_root
- topology: block_topo
  cluster_default: false
  block:
    block_sizes:
      - 4
    blocks:
      - block: b1
""",
    )

    atf.require_config_parameter(
        "PartitionName",
        {
            "tree": {"Nodes": "ALL", "Topology": "tree_topo"},
            "block": {"Nodes": "ALL", "Topology": "block_topo"},
        },
    )

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def dynamic_nodes():
    """Register the dynamic nodes once for the whole module in register_order,
    each placed under switch sw_root and block b1."""
    base_port = 61200
    started = []
    for i, name in enumerate(register_order):
        port = base_port + i
        atf.run_command(
            f"{atf.properties['slurm-sbin-dir']}/slurmd -N {name} -Z -b "
            f"--conf 'Port={port} Topology=tree_topo:sw_root,block_topo:b1'",
            user="root",
            fatal=True,
        )
        started.append(name)
        atf.repeat_until(
            lambda n=name: n in atf.get_nodes(quiet=True),
            lambda found: found,
            timeout=30,
            fatal=True,
        )

    for name in register_order:
        assert atf.wait_for_node_state(
            name, "IDLE", timeout=30
        ), f"Dynamic node {name} should reach IDLE state"

    yield

    for name in started:
        # Make sure no jobs are running on the node so we can delete it cleanly.
        atf.repeat_until(
            lambda n=name: atf.get_node_parameter(n, "state"),
            lambda states: "ALLOCATED" not in states and "MIXED" not in states,
            fatal=False,
        )
        pid = atf.run_command_output(
            f"pgrep -f '{atf.properties['slurm-sbin-dir']}/slurmd -N {name}'",
            fatal=False,
        ).strip()
        if pid:
            atf.run_command(f"kill {pid}", user="root", fatal=False)
        atf.run_command(f"scontrol delete NodeName={name}", user="slurm", fatal=False)


def _parse_task_nodes(output):
    """Parse 'tid: nodename' lines, return list of nodes ordered by tid."""
    matches = re.findall(r"(\d+): (\S+)", output)
    matches.sort(key=lambda x: int(x[0]))
    return [name for _, name in matches]


def _run_step_layout(partition):
    return atf.run_job_output(
        f"-p {partition} --nodelist={node_list_arg} -N{nnodes} -n{nnodes} "
        f"-m block --exclusive --mem=1 -l printenv SLURMD_NODENAME",
        fatal=True,
    )


def test_alpha_sort_within_switch(dynamic_nodes):
    """topology/tree lays out tasks in alphabetical node order within a switch."""

    output = _run_step_layout("tree")
    actual = _parse_task_nodes(output)
    assert actual == alpha_order, (
        "Tasks should follow alphabetical node order within the switch: "
        f"expected {alpha_order}, got {actual}"
    )


def test_alpha_sort_within_block(dynamic_nodes):
    """topology/block lays out tasks in alphabetical node order within a block."""

    output = _run_step_layout("block")
    actual = _parse_task_nodes(output)
    assert actual == alpha_order, (
        "Tasks should follow alphabetical node order within the block: "
        f"expected {alpha_order}, got {actual}"
    )
