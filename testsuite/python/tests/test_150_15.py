############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmd message forwarding with a tree width below the node count.

Since 23.11 slurmds also forward to dynamic nodes, which they do not know from
slurm.conf, and to CLOUD nodes with no address in slurm.conf, so TreeWidth no
longer has to disable the fanout for them.
"""

import re

import pytest

import atf

# Above the width of the reverse tree that step completions go up (7)
NODE_COUNT = 8
# The job end goes through the first of these nodes to the second one
JOB_END_NODES = 3
CLOUD_NODES = [f"cloud{i}" for i in range(1, NODE_COUNT + 1)]
# Below JOB_END_NODES, so that slurmctld must send through a slurmd, and not 1,
# so that its forwards do not mix with the srun --treewidth=1 ones
TREE_WIDTH = 2
# A slurmd that forwarded may only fail after it replies, so each test runs
# its job twice on the same nodes
RUNS = 2
# The sender logs this when a slurmd did not forward, and then sends to the
# other nodes itself, so the job can still work
FORWARD_ERROR = "failed to forward the message"
# Above the ports the kernel picks for other sockets (32768-60999 by default),
# so that none can take a port before its slurmd binds it
PORT_BASE = 61700


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(NODE_COUNT)
    atf.require_config_parameter("TreeWidth", TREE_WIDTH)
    # A forwarding slurmd logs the nodes it forwards the message to
    atf.require_config_parameter_includes("DebugFlags", "Route")
    # A slurmd logs each RPC it processes at debug2
    atf.require_config_parameter(
        "SlurmdDebug", ["debug2", "debug3", "debug4", "debug5"]
    )
    # Both route by switch or partition instead of by tree width
    atf.require_config_parameter_excludes("TopologyParam", "RouteTree")
    atf.require_config_parameter_excludes("TopologyParam", "RoutePart")
    # slurmctld only learns the address of these nodes when they register.
    # A NodeName dict replaces every node line, so keep the others.
    node_lines = atf.get_config(live=False, quiet=True)["NodeName"]
    atf.require_config_parameter(
        "NodeName",
        node_lines
        | {
            node: {"State": "CLOUD", "Port": PORT_BASE + NODE_COUNT + i}
            for i, node in enumerate(CLOUD_NODES)
        },
    )
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter(
        "MaxNodeCount", len(atf.get_nodes(live=False, quiet=True)) + NODE_COUNT
    )
    # Required for MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_slurm_running()


def sorted_nodes(node_list):
    """The nodes in the order Slurm sorts a node list."""
    return atf.node_range_to_list(atf.node_list_to_range(node_list))


@pytest.fixture(scope="module", params=["static", "dynamic", "mixed", "cloud"])
def nodes(request):
    """NODE_COUNT static nodes, dynamic nodes, half of each, or CLOUD nodes.

    The dynamic nodes sort after the static ones, so that a static slurmd
    forwards to a dynamic node.
    """
    if request.param == "cloud":
        # require_slurm_running() started their slurmds with the others
        return sorted_nodes(CLOUD_NODES)

    dynamic_count = {"static": 0, "dynamic": NODE_COUNT, "mixed": NODE_COUNT // 2}
    dynamic = [f"node{100 + i}" for i in range(dynamic_count[request.param])]
    if dynamic:
        request.addfinalizer(lambda: remove_dynamic_nodes(dynamic))
        for i, node in enumerate(dynamic):
            atf.start_slurmd(node, f"-Z -b --conf 'Port={PORT_BASE + i}'")
        atf.wait_for_node_state(dynamic, "IDLE", fatal=True)

    static = sorted_nodes(
        [
            node
            for node in atf.get_nodes(live=False, fatal=True)
            if node != "DEFAULT" and node not in CLOUD_NODES
        ]
    )
    return sorted_nodes(static[: NODE_COUNT - len(dynamic)] + dynamic)


def remove_dynamic_nodes(dynamic):
    """Stop the slurmds of the dynamic nodes and delete the nodes."""
    errors = []
    for node in dynamic:
        atf.stop_slurmd(node, quiet=True)
        if node in atf.get_nodes(quiet=True):
            result = atf.run_command(
                f"scontrol delete NodeName={node}",
                user=atf.properties["slurm-user"],
                quiet=True,
            )
            if result["exit_code"] != 0:
                errors.append(f"Failed to delete node {node}: {result['stderr']}")
    if errors:
        pytest.fail("; ".join(errors))


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
        f"-N{len(nodes)} -w {','.join(nodes)} --treewidth={width}"
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

    # The job end must reach each slurmd, so this fails if one of them died.
    # A dynamic node is also DYNAMIC_NORM, and a CLOUD node CLOUD.
    for _ in atf.timer():
        node_states = atf.get_nodes(quiet=True)
        not_idle = {
            node: node_states[node]["state"]
            for node in nodes
            if set(node_states[node]["state"]) - {"DYNAMIC_NORM", "CLOUD"} != {"IDLE"}
        }
        if not not_idle:
            break
    assert not not_idle, f"each node must be IDLE again after the job, got {not_idle}"
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
    # Start at the last node before the dynamic ones, so that in the mixed case
    # a static slurmd forwards the job end to a dynamic node
    states = atf.get_nodes(quiet=True)
    dynamic = [
        i for i, node in enumerate(nodes) if "DYNAMIC_NORM" in states[node]["state"]
    ]
    start = max(dynamic[0] - 1, 0) if dynamic else 0
    nodes = nodes[start : start + JOB_END_NODES]
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


def test_setdebug(nodes, slurmd_logs):
    """scontrol setdebug nodes= reaches each slurmd.

    scontrol gets the addresses of the nodes from slurmctld and sends the
    request through the slurmds with TreeWidth. The level stays the one in
    slurm.conf, so the other tests log the same.
    """
    level = atf.get_config_parameter("SlurmdDebug")
    result = atf.run_command(
        f"scontrol setdebug {level} nodes={atf.node_list_to_range(nodes)}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        FORWARD_ERROR not in result["stderr"]
    ), f"scontrol must not fail to forward: {result['stderr']}"
    for node, log_file in slurmd_logs.items():
        assert grep_log(
            log_file, "REQUEST_SET_DEBUG_LEVEL"
        ), f"{node} must get the debug level request"
    forwards = {node: forwarded(log, TREE_WIDTH) for node, log in slurmd_logs.items()}
    assert any(
        forwards.values()
    ), f"a slurmd must forward the request with tree width {TREE_WIDTH}, got {forwards}"


def test_getaddrs(nodes):
    """scontrol getaddrs reports the address and port of each node."""
    output = atf.run_command_output(
        f"scontrol getaddrs {atf.node_list_to_range(nodes)}", fatal=True
    )
    # Each line is "<node>: <address>:<port>"
    addrs = dict(line.split(": ", 1) for line in output.splitlines())
    assert set(addrs) == set(nodes), f"getaddrs must report each node, got {output}"
    for node, addr in addrs.items():
        address, port = addr.rsplit(":", 1)
        assert address not in (
            "0.0.0.0",
            "[::]",
        ), f"{node} must be reported with an address, got {addr}"
        assert int(port) == atf.get_node_parameter(
            node, "port"
        ), f"{node} must be reported with its port, got {addr}"
