############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test single-node job placement with enable_one_node_select."""

import collections
import datetime
import re

import pytest

import atf

NODE_COUNT = 8
CPUS_PER_NODE = 4
NODE_NAMES = [f"node{i}" for i in range(1, NODE_COUNT + 1)]
LICENSE = "bftest"
SCHED_BASELINE = "bf_interval=1,sched_interval=1"

PARTITIONS = {"flat": "flat_topo", "tree": "tree_topo"}
FAST_PARTITION = "flat_fast"
ALL_PARTITIONS = {**PARTITIONS, FAST_PARTITION: "flat_fast_topo"}

TOPOLOGY_YAML = """
- topology: flat_topo
  cluster_default: true
  flat: true
- topology: tree_topo
  cluster_default: false
  tree:
    switches:
      - switch: root
        children: s[1-2]
      - switch: s1
        nodes: node[1-4]
      - switch: s2
        nodes: node[5-8]
- topology: flat_fast_topo
  cluster_default: false
  enable_one_node_select: true
  flat: true
"""

# A job start can span a slurmctld re-exec.
JOB_START_TIMEOUT = 60
# These jobs start in about half a second, and there are dozens of waits.
POLL_INTERVAL = 0.1

pytestmark = pytest.mark.slow


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (25, 5), "bin/scontrol", reason="The topology.yaml option was added in 25.05"
    )
    atf.require_version(
        (26, 11),
        reason="Ticket 25772: enable_one_node_select in topology.yaml is new "
        "in 26.11",
    )
    atf.require_nodes(NODE_COUNT, [("CPUs", CPUS_PER_NODE)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_file("topology.yaml", TOPOLOGY_YAML)
    atf.require_config_parameter(
        "PartitionName",
        {
            name: {"Nodes": "ALL", "Topology": topology}
            for name, topology in ALL_PARTITIONS.items()
        },
    )
    atf.require_config_parameter("SchedulerParameters", SCHED_BASELINE)

    # Blocks the bf_busy_nodes probe without using node resources.
    atf.require_config_parameter("Licenses", f"{LICENSE}:1")

    atf.require_slurm_running()

    # _eval_nodes_serial() and _eval_nodes_busy() iterate weight groups first.
    weights = {atf.get_node_parameter(name, "weight") for name in NODE_NAMES}
    assert len(weights) == 1, f"This test requires a uniform node Weight, got {weights}"


@pytest.fixture(scope="function", autouse=True)
def cleanup():
    yield
    atf.cancel_all_jobs(fatal=True, quiet=True)
    for partition in ALL_PARTITIONS:
        atf.run_command(
            f"scontrol update PartitionName={partition} LLN=no",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


def configure(select_params="CR_CPU", sched_params=""):
    # Each write re-execs slurmctld, so only write what differs.
    # SelectTypeParameters can only change on a restart.
    sched = f"{SCHED_BASELINE},{sched_params}" if sched_params else SCHED_BASELINE
    settings = [
        ("SchedulerParameters", sched, False),
        ("SelectTypeParameters", select_params, True),
    ]
    for name, value, restart in settings:
        # get_config_parameter() returns the value casefolded.
        if atf.get_config_parameter(name, live=False) != value.casefold():
            atf.set_config_parameter(name, value, restart=restart)

    # A restart does not wait for nodes to re-register.
    reset_to_idle()


def set_partition_lln(partition, enabled):
    # Must follow configure(); a reconfigure discards runtime partition updates.
    atf.run_command(
        f"scontrol update PartitionName={partition} "
        f"LLN={'yes' if enabled else 'no'}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def run_single_node_jobs(partition, count):
    # One at a time, so each job is placed against the previous one's usage.
    nodes = []
    for i in range(count):
        job_id = atf.submit_job_sbatch(
            f'-p {partition} -N1 -n1 --mem=1 --wrap="sleep infinity"', fatal=True
        )
        assert atf.wait_for_job_state(
            job_id, "RUNNING", timeout=JOB_START_TIMEOUT, poll_interval=POLL_INTERVAL
        ), f"Verify that single-node job {i + 1} of {count} started"
        nodes.append(atf.get_job_parameter(job_id, "NodeList", fatal=True))
    return nodes


def reset_to_idle():
    atf.cancel_all_jobs(fatal=True, quiet=True)
    atf.wait_for_node_state(NODE_NAMES, "IDLE", poll_interval=POLL_INTERVAL, fatal=True)


def probe_future_placement(partition):
    """Return the nodes srun --test-only plans for a license-blocked job."""
    result = atf.run_command(
        f"srun --test-only -p {partition} -N1 -n1 --mem=1 -L {LICENSE}:1 -t 1 "
        "/bin/true",
        env_vars="SLURM_TIME_FORMAT=standard",
        fatal=True,
    )
    output = result["stderr"] + result["stdout"]

    # bf_busy_nodes is only used by _future_run_test().
    start_match = re.search(r"to start at (\S+)", output)
    assert start_match, f"Verify that srun --test-only reported a start time: {output}"
    start_time = datetime.datetime.strptime(start_match.group(1), "%Y-%m-%dT%H:%M:%S")
    assert start_time > datetime.datetime.now(), (
        f"Verify the probe was deferred into _future_run_test() rather than "
        f"answered by the run-now pass; it reported {start_match.group(1)}"
    )

    match = re.search(r"on nodes (\S+) in partition", output)
    assert match, f"Verify that srun --test-only reported a placement: {output}"

    return atf.node_range_to_list(match.group(1))


@pytest.mark.parametrize("partition", PARTITIONS)
def test_lln_partition_honored_single_node(partition):
    """Partition LLN=YES spreads single-node jobs."""
    configure()

    set_partition_lln(partition, False)
    stacked = run_single_node_jobs(partition, 4)
    assert len(set(stacked)) == 1, (
        f"Verify that without LLN the four single-node jobs share one node, "
        f"got {stacked}"
    )

    reset_to_idle()

    # Past NODE_COUNT, "first empty node" would double up on one node.
    set_partition_lln(partition, True)
    counts = collections.Counter(run_single_node_jobs(partition, NODE_COUNT + 4))
    assert len(counts) == NODE_COUNT, (
        f"Verify that partition LLN=YES used every node before doubling up "
        f"on any, got {dict(counts)}"
    )
    assert max(counts.values()) - min(counts.values()) <= 1, (
        f"Verify that LLN kept per-node job counts balanced once no node was "
        f"empty, which only ranking by idle CPUs achieves; got {dict(counts)}"
    )


def test_lln_partition_ignored_with_one_node_select():
    """With enable_one_node_select, LLN=YES does not spread single-node jobs."""
    configure()
    set_partition_lln(FAST_PARTITION, True)

    nodes = run_single_node_jobs(FAST_PARTITION, 4)
    assert len(set(nodes)) < 4, (
        f"Verify that with enable_one_node_select LLN=YES does not spread "
        f"single-node jobs, got {nodes}"
    )


@pytest.mark.parametrize("partition", PARTITIONS)
def test_lln_global_honored_single_node(partition):
    """SelectTypeParameters=CR_LLN spreads single-node jobs."""
    configure(select_params="CR_CPU,CR_LLN")
    set_partition_lln(partition, False)

    spread = run_single_node_jobs(partition, 4)
    assert len(set(spread)) == 4, (
        f"Verify that CR_LLN sends each single-node job to a different node, "
        f"got {spread}"
    )


@pytest.mark.parametrize("partition", PARTITIONS)
def test_pack_serial_at_end_single_node(partition):
    """pack_serial_at_end sends a serial job to the last node."""
    last_node = f"node{NODE_COUNT}"

    configure()
    baseline = run_single_node_jobs(partition, 1)[0]
    assert baseline != last_node, (
        f"Verify that without pack_serial_at_end the serial job does not take "
        f"the last node, got {baseline}"
    )

    reset_to_idle()

    configure(sched_params="pack_serial_at_end")
    packed = run_single_node_jobs(partition, 1)[0]
    assert packed == last_node, (
        f"Verify that pack_serial_at_end sends the serial job to {last_node}, "
        f"got {packed}"
    )


@pytest.mark.parametrize("partition", PARTITIONS)
def test_bf_busy_nodes_single_node(partition):
    """bf_busy_nodes plans a single-node job onto a node already in use."""
    configure(sched_params="bf_busy_nodes")

    busy = f"node{NODE_COUNT}"

    # Holds the license; its -t 5 end is the planned start, when every node,
    # busy included, is free.
    blocker = atf.submit_job_sbatch(
        f"-p {partition} -w {busy} -N1 -n1 --mem=1 -L {LICENSE}:1 -t 5 "
        '--wrap="sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        blocker, "RUNNING", timeout=JOB_START_TIMEOUT, poll_interval=POLL_INTERVAL
    ), "Verify that the license blocker job started"

    planned = probe_future_placement(partition)
    assert planned == [busy], (
        f"Verify that bf_busy_nodes planned the job onto in-use {busy}, "
        f"but got {planned}"
    )
