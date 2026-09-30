############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmd message forwarding with a tree width below the node count."""

import re

import pytest

import atf

NODE_COUNT = 3
# Below NODE_COUNT, so that slurmctld must send through a slurmd, and not 1,
# so that its forwards do not mix with the srun --treewidth=1 ones
TREE_WIDTH = 2
# A slurmd that forwarded may only fail after it replies, so each test runs
# its job twice on the same nodes
RUNS = 2
# The sender logs this when a slurmd did not forward, and then sends to the
# other nodes itself, so the job can still work
FORWARD_ERROR = "failed to forward the message"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(NODE_COUNT)
    atf.require_config_parameter("TreeWidth", TREE_WIDTH)
    # A forwarding slurmd logs the nodes it forwards the message to
    atf.require_config_parameter_includes("DebugFlags", "Route")
    # Both route by switch or partition instead of by tree width
    atf.require_config_parameter_excludes("TopologyParam", "RouteTree")
    atf.require_config_parameter_excludes("TopologyParam", "RoutePart")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes():
    """The first NODE_COUNT nodes, in the order Slurm sorts a node list."""
    node_range = atf.node_list_to_range(list(atf.get_nodes(fatal=True)))
    return atf.node_range_to_list(node_range)[:NODE_COUNT]


@pytest.fixture(scope="module")
def slurmd_logs(nodes):
    """Log file of the slurmd of each node."""
    log_file = atf.get_config()["SlurmdLogFile"]
    return {node: log_file.replace("%n", node) for node in nodes}


@pytest.fixture(scope="module")
def slurmctld_log():
    """Log file of slurmctld."""
    return atf.get_config()["SlurmctldLogFile"]


@pytest.fixture(autouse=True)
def empty_logs(slurmd_logs, slurmctld_log):
    """Empty the logs, so each test reads only its own lines."""
    atf.run_command(
        f"truncate -s 0 {slurmctld_log} {' '.join(slurmd_logs.values())}",
        user="root",
        fatal=True,
        quiet=True,
    )


def grep_log(log_file, text):
    """Lines of a root-owned log file that contain text."""
    # grep exits with 1 when no line matches, and with 2 when it fails
    return atf.run_command_output(
        f"grep -F -- '{text}' {log_file} || [ $? -eq 1 ]",
        user="root",
        fatal=True,
        quiet=True,
    )


def forwarded(log_file, width):
    """Node lists that the slurmd of a log file forwarded to with width."""
    return re.findall(
        rf"hl=(\S+) tree_width {width}$",
        grep_log(log_file, "ROUTE: split_hostlist:"),
        re.MULTILINE,
    )


def run_on_all_nodes(nodes, slurmctld_log, width):
    """Run one task on each node with srun --treewidth=width.

    Each node must run its task and be IDLE again after the job, and neither
    srun nor slurmctld may fail to forward a message.
    """
    result = atf.run_job(
        f"-N{NODE_COUNT} -w {','.join(nodes)} --treewidth={width}"
        " printenv SLURMD_NODENAME"
    )
    assert (
        result["exit_code"] == 0
    ), f"srun --treewidth={width} must succeed: {result['stderr']}"
    tasks = sorted(result["stdout"].split())
    assert tasks == sorted(nodes), f"each node must run one task, got {tasks}"
    assert (
        FORWARD_ERROR not in result["stderr"]
    ), f"srun must not fail to forward: {result['stderr']}"

    # The job end must reach each slurmd, so this fails if one of them died
    assert atf.wait_for_node_state(
        nodes, "IDLE", operator=atf.SetMatch.EQUAL
    ), "each node must be IDLE again after the job"
    errors = grep_log(slurmctld_log, FORWARD_ERROR)
    assert errors == "", f"slurmctld must not fail to forward: {errors}"


def test_srun_treewidth(nodes, slurmd_logs, slurmctld_log):
    """slurmds forward the launch of srun --treewidth=1.

    srun sends the launch to the first node only. Each slurmd forwards the
    rest of the node list with the same width, so every node but the last one
    forwards once per launch.
    """
    for _ in range(RUNS):
        run_on_all_nodes(nodes, slurmctld_log, 1)

    expected = {node: [] for node in nodes}
    for i, node in enumerate(nodes[:-1]):
        expected[node] = [atf.node_list_to_range(nodes[i + 1 :])] * RUNS
    forwards = {node: forwarded(log, 1) for node, log in slurmd_logs.items()}
    assert forwards == expected, (
        "each slurmd but the last one must forward the launch to the nodes"
        f" after it, expected {expected}, got {forwards}"
    )


def test_job_end(nodes, slurmd_logs, slurmctld_log):
    """slurmctld sends the job end through a slurmd.

    TreeWidth is below the node count, so slurmctld cannot send the job end
    to every node itself, and a slurmd must forward it. The launch uses
    --treewidth=off, so srun sends it to every node itself.
    """
    for _ in range(RUNS):
        run_on_all_nodes(nodes, slurmctld_log, "off")

    # Each job end makes the first node forward once to the second one, and so
    # does each prolog launch with PrologFlags=Alloc
    per_job = 2 if atf.config_parameter_includes("PrologFlags", "Alloc") else 1
    expected = [nodes[1]] * per_job * RUNS
    forwards = forwarded(slurmd_logs[nodes[0]], TREE_WIDTH)
    assert forwards == expected, (
        f"{nodes[0]} must forward each job end with tree width {TREE_WIDTH},"
        f" expected {expected}, got {forwards}"
    )
