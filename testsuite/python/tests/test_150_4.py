############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import re

import atf
import pytest

register_order = ["b2", "a2", "b1", "a1"]
alpha_order = ["a1", "a2", "b1", "b2"]
node_list_arg = ",".join(alpha_order)
nnodes = len(register_order)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Wants to set required dynamic node parameters")
    atf.require_version((26, 5), "sbin/slurmd")
    # Bootstrap with one static node; dynamic nodes register on top.
    atf.require_nodes(1)
    atf.require_config_parameter("MaxNodeCount", nnodes + 1)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    # Required for MaxNodeCount.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    atf.require_config_file(
        "topology.yaml",
        """
- topology: topo_alpha
  cluster_default: true
  flat:
    alpha_step_rank: true
- topology: topo_plain
  cluster_default: false
  flat: true
""",
    )

    atf.require_config_parameter(
        "PartitionName",
        {
            "alpha": {"Nodes": "ALL", "Topology": "topo_alpha"},
            "plain": {"Nodes": "ALL", "Topology": "topo_plain"},
        },
    )

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def dynamic_nodes():
    """Register the dynamic nodes once for the whole module in register_order."""
    base_port = 61000
    started = []
    for i, name in enumerate(register_order):
        port = base_port + i
        atf.run_command(
            f"{atf.properties['slurm-sbin-dir']}/slurmd -N {name} -Z -b "
            f"--conf 'Port={port}'",
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


def test_alpha_step_rank_enabled(dynamic_nodes):
    """With alpha_step_rank, tasks are laid out in alphabetical node order."""

    output = _run_step_layout("alpha")
    actual = _parse_task_nodes(output)
    assert actual == alpha_order, (
        f"Tasks should follow alphabetical node order: "
        f"expected {alpha_order}, got {actual}"
    )


def test_alpha_step_rank_disabled(dynamic_nodes):
    """Without alpha_step_rank, tasks follow node-table (registration) order."""

    output = _run_step_layout("plain")
    actual = _parse_task_nodes(output)
    assert actual == register_order, (
        f"Tasks should follow node-table order: "
        f"expected {register_order}, got {actual}"
    )
