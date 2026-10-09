############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Single-node jobs must honor LLN, bf_busy_nodes and pack_serial_at_end.

_select_nodes() has a dedicated fast path for max_nodes == 1 jobs
(_get_one_res()) that skips eval_nodes(), and with it the whole _eval_nodes_*
dispatch ladder where --spread-job, bf_busy_nodes, LLN and pack_serial_at_end
are implemented. Adding that fast path silently disabled the last three for
single-node jobs.

_allow_fast_path() now declines that fast path whenever any of the three
is active, so the job falls through to eval_nodes() and reaches
_eval_nodes_busy(), _eval_nodes_lln() or _eval_nodes_serial() the way it did
before 26.05. That check lives in cons_tres and is topology-agnostic: every
plugin declines the fast path under those options.

Declining it only changes the outcome where eval_nodes() actually reaches the
ladder, and that is topology/flat and topology/tree -- both leave trump_others
false. Those are the two plugins under test, and every case runs once per
plugin. The property under test is that a single-node job honors these three
options the way a multi-node job already does, identically under both.

The other three plugins are out of scope, for two different reasons.
topology/torus3d never took the fast path at all: its topology_p_allow_one_node()
returns false unconditionally, and it is the conjunct evaluated before
_allow_fast_path(), so this change cannot reach it. topology/block and
topology/ring set trump_others only for jobs whose candidate nodes fall inside a
configured block or ring; for those -- the normal case -- their own evaluator
preempts the ladder, so the options stay inoperative and the job merely loses
the 26.05 fast path and shifts from sched_weight order to topology-aware
best-fit. A job on nodes outside every configured block or ring does reach the
ladder and is affected, but that configuration is not covered here.
"""

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

# Both topologies leave trump_others false, so eval_nodes() reaches the
# _eval_nodes_* ladder for them and the expected placements are identical.
# Each has its own partition over the same nodes, so a case picks its topology
# with -p rather than with a reconfigure.
PARTITIONS = {"flat": "flat_topo", "tree": "tree_topo"}

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
"""

# A job start has to survive a controller re-exec, which can exceed atf's
# default_polling_timeout of 45s on a loaded runner.
JOB_START_TIMEOUT = 60

# atf polls once a second for a timeout that long, but these jobs start in about
# half a second, and there are dozens of waits.
POLL_INTERVAL = 0.1

# Fifty-two job start waits and three slurmctld starts take about a minute;
# tox.ini calls >60s slow.
pytestmark = pytest.mark.slow


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to set scheduler, select and license parameters")
    atf.require_version(
        (26, 5, 5),
        reason="Ticket 25772: single-node jobs bypass LLN, bf_busy_nodes and "
        "pack_serial_at_end from 26.05.0 until the fix in 26.05.5 and 26.11",
    )
    atf.require_nodes(NODE_COUNT, [("CPUs", CPUS_PER_NODE)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_file("topology.yaml", TOPOLOGY_YAML)
    atf.require_config_parameter(
        "PartitionName",
        {
            name: {"Nodes": "ALL", "Topology": topology}
            for name, topology in PARTITIONS.items()
        },
    )

    # Owned outright rather than appended to, so that configure() can rebuild
    # SchedulerParameters from a known baseline instead of guessing what else
    # the running configuration put there.
    atf.require_config_parameter("SchedulerParameters", SCHED_BASELINE)

    # Blocks the bf_busy_nodes probe without consuming any node resources.
    atf.require_config_parameter("Licenses", f"{LICENSE}:1")

    atf.require_slurm_running()

    # The pack_serial_at_end and bf_busy_nodes cases are weight-sensitive:
    # _build_node_weight_list() is the outer loop in both _eval_nodes_serial()
    # and _eval_nodes_busy(), so a node with a different Weight lands in a
    # group of its own and silently changes which node wins. Nothing in this
    # file sets Weight, so uniformity is inherited from require_nodes()
    # cloning one template node -- true today, but not a documented ATF
    # contract. Assert it, so a future change surfaces as a precondition
    # failure rather than a baffling placement failure.
    weights = {atf.get_node_parameter(name, "weight") for name in NODE_NAMES}
    assert len(weights) == 1, f"This test requires a uniform node Weight, got {weights}"


@pytest.fixture(scope="function", autouse=True)
def cleanup():
    """Undo per-test runtime state so tests do not depend on each other."""
    yield
    atf.cancel_all_jobs(fatal=True, quiet=True)
    for partition in PARTITIONS:
        atf.run_command(
            f"scontrol update PartitionName={partition} LLN=no",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


def configure(select_params="CR_CPU", sched_params=""):
    """Put the cluster into a known state for one test.

    Every test calls this first, so no test depends on the order it runs in or
    on what a previous test left behind.

    Only a parameter whose value differs from slurm.conf is written, because
    every write replaces the slurmctld process: set_config_parameter()'s
    restart=False path is scontrol reconfigure rather than nothing, and
    reconfigure fork+execs a fresh controller. Most cases share their settings
    with the case before, so most calls write nothing.

    SelectTypeParameters is written with restart=True: slurmctld refuses to
    change it on a reconfigure (_preserve_select_type_param() reverts the new
    value and returns ESLURM_INVALID_SELECTTYPE_CHANGE), so it is only safe to
    write it with the restart that will apply it.
    """
    sched = f"{SCHED_BASELINE},{sched_params}" if sched_params else SCHED_BASELINE
    settings = [
        ("SchedulerParameters", sched, False),
        ("SelectTypeParameters", select_params, True),
    ]
    for name, value, restart in settings:
        # get_config_parameter() returns the value casefolded.
        if atf.get_config_parameter(name, live=False) != value.casefold():
            atf.set_config_parameter(name, value, restart=restart)

    # set_config_parameter(restart=True) only waits for scontrol ping to report
    # UP, not for nodes to finish re-registering, and cleanup's cancel_all_jobs()
    # waits on job state rather than on nodes leaving COMPLETING. Every assertion
    # in this file names a specific node, so a node that is transiently not a
    # candidate changes the answer. That is a hard failure for pack_serial_at_end,
    # which asserts the last node exactly, and a soft one everywhere else.
    reset_to_idle()


def set_partition_lln(partition, enabled):
    """Toggle LLN on one partition.

    Only ever called after configure(): a reconfigure or restart there rereads
    slurm.conf and would discard a runtime partition update.
    """
    atf.run_command(
        f"scontrol update PartitionName={partition} "
        f"LLN={'yes' if enabled else 'no'}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def run_single_node_jobs(partition, count):
    """Submit `count` one-CPU single-node jobs and return the nodes they got.

    Submitted one at a time, each waiting to start before the next goes in, so
    every job is placed against the usage the previous one left behind. That is
    what makes the LLN spread deterministic rather than dependent on how many
    jobs a single scheduling cycle happens to place.

    The partition, which picks the topology, is a required argument rather than
    a default, so that a case cannot silently end up covering only one of the
    two topologies under test.
    """
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
    """Return the nodes reported for a license-blocked single-node job.

    srun --test-only runs job_start_data() -> _will_run_test() ->
    _future_run_test() synchronously and reports the result on stderr.

    Also asserts the reported start time is in the future. bf_busy_nodes is
    only consulted by _future_run_test(); if the license failed to block, the
    "can it run now" pass in _will_run_test() would answer first with
    prefer_alloc_nodes=false and the probe would report an idle node that
    looks like a real result. Checking the start time makes the probe prove it
    was actually deferred rather than assuming it.

    Returns a list, and it can hold more than one node even for a -N1 job. A
    26.11.0-0rc1 run of this module reported "on nodes node[1,7]" for the
    bf_busy_nodes on-arm, on the same line that reported 1 processor -- so the
    allocation is one node while the bitmap srun prints is wider than it. Why
    has not been established; it is not the narrowing in common_test_node() or
    the _eval_nodes_* arms, which all rebuild node_map from bit_clear_all().
    Callers therefore assert on which nodes appear, never on how many.

    SLURM_TIME_FORMAT is pinned because slurm_make_time_str() honors it -- a
    runner exporting "relative" would make the strptime() below raise instead
    of asserting, and the failure would not look related to this test.
    """
    result = atf.run_command(
        f"srun --test-only -p {partition} -N1 -n1 --mem=1 -L {LICENSE}:1 -t 1 "
        "/bin/true",
        env_vars="SLURM_TIME_FORMAT=standard",
        fatal=True,
    )
    output = result["stderr"] + result["stdout"]

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
    """Partition LLN=YES ranks single-node jobs by idle CPUs.

    The off arm shows the jobs are free to pile onto one node, so the on arm
    is not just observing that they had nowhere to stack.

    The on arm runs past NODE_COUNT so that no node is left empty. Up to that
    point every round has a completely empty node, so "pick the first empty
    node" produces one job per node just as the idle-CPU ranking does; past
    it, that policy doubles up on a single node and the balance assertion
    rejects it.

    The two assertions do different work. The real pre-fix behavior stacks
    onto the first node with a free CPU, using three nodes of four, so the
    distinct-node assertion is what catches the regression. The balance
    assertion rules out first-empty-node. Neither separates LLN from plain
    round robin, which produces identical counts -- no policy here does that,
    so do not read the balance check as proving the ranking is uniquely by
    idle CPUs.
    """
    configure()

    set_partition_lln(partition, False)
    stacked = run_single_node_jobs(partition, 4)
    assert len(set(stacked)) == 1, (
        f"Verify that without LLN the four single-node jobs share one node, "
        f"got {stacked}"
    )

    reset_to_idle()

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


@pytest.mark.parametrize("partition", PARTITIONS)
def test_lln_global_honored_single_node(partition):
    """SelectTypeParameters=CR_LLN also applies to single-node jobs.

    Covers the other arm of the check in _allow_fast_path(), which tests
    cr_type & SELECT_LLN separately from the partition flag. The
    partition flag is explicitly cleared so only the global setting can be
    responsible for the result.

    This case asserts only that the jobs spread. Both the "they would
    otherwise stack" control and the check that the spread actually ranks by
    idle CPUs live in test_lln_partition_honored_single_node(); this one exists
    to cover the cr_type arm of the veto, not to re-prove the criterion.
    """
    configure(select_params="CR_CPU,CR_LLN")
    set_partition_lln(partition, False)

    spread = run_single_node_jobs(partition, 4)
    assert len(set(spread)) == 4, (
        f"Verify that CR_LLN sends each single-node job to a different node, "
        f"got {spread}"
    )


@pytest.mark.parametrize("partition", PARTITIONS)
def test_pack_serial_at_end_single_node(partition):
    """pack_serial_at_end sends a serial job to the far end of the node list.

    With the option on, the job declines the fast path and reaches
    _eval_nodes_serial(), which walks node indexes backwards inside each node
    weight group, so the highest numbered node with free resources wins. With
    it off, the job takes the max_nodes == 1 fast path in _get_one_res(),
    which takes the first match in sched_weight order.

    Node weights must stay uniform here. _eval_nodes_serial() iterates weight
    groups as its outer loop, so distinct weights would put node1 alone in the
    first group and it would win the backwards scan outright -- the exact
    opposite of what the option is for.

    The off arm asserts only that the job did not land on the last node. With
    uniform weights _cmp_node() returns 0 for every pair, and qsort is not
    stable, so strictly nothing about the resulting order is guaranteed; in
    practice glibc insertion-sorts small arrays and preserves index order, so
    the first node wins.

    The option applies to genuinely serial jobs only. Both the fast path veto
    in _allow_fast_path() and the dispatch in eval_nodes() gate on
    details->min_cpus == 1 and req_nodes == 1, which is what -N1 -n1 gives.
    """
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
@pytest.mark.parametrize("busy_nodes", [False, True])
def test_bf_busy_nodes_single_node(busy_nodes, partition):
    """bf_busy_nodes steers a single-node job onto a node already in use.

    bf_busy_nodes reaches the selector on exactly one path, _future_run_test(),
    the only _job_test() caller that passes it as prefer_alloc_nodes; every
    other caller hardcodes false. A job that can start immediately therefore
    never sees the option, so the probe has to be blocked -- and blocked by
    something other than node capacity, since an idle node would otherwise just
    absorb it and leave nothing to choose between. A single-count license does
    that: it is orthogonal to node selection, so idle nodes can coexist with a
    job that cannot start yet.

    The on arm names the anchor node rather than accepting either in-use node.
    _eval_nodes_busy() scans ascending within its non-idle pass, so the lower
    indexed of the two is deterministic. The other in-use node is the weaker
    evidence anyway: it is free in the future simulation and only counts as
    busy through the live idle_node_bitmap, which makes it the plausible
    accidental pick if something unrelated regressed. Weights have to stay
    uniform here because node weight is the outer loop in _eval_nodes_busy().

    Note this option shapes the planned placement only. When the job really
    starts it is reselected through SELECT_MODE_RUN_NOW, which passes
    prefer_alloc_nodes=false, so watching where a job finally lands would not
    test this.
    """
    configure(sched_params="bf_busy_nodes" if busy_nodes else "")

    busy = [f"node{NODE_COUNT - 1}", f"node{NODE_COUNT}"]
    idle = [name for name in NODE_NAMES if name not in busy]

    # Anchor: keeps busy[0] in use for the whole test while leaving CPUs free
    # on it, so the probe can actually be planned there.
    anchor = atf.submit_job_sbatch(
        f'-p {partition} -w {busy[0]} -N1 -n1 --mem=1 -t 20 --wrap="sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        anchor, "RUNNING", timeout=JOB_START_TIMEOUT, poll_interval=POLL_INTERVAL
    ), "Verify that the anchor job started"

    # Blocker: exhausts the license and puts busy[1] in use. sleep infinity
    # leaves -t 5 as the only thing that ends it, which is the end time
    # _future_run_test() plans against and the first viable future slot.
    blocker = atf.submit_job_sbatch(
        f"-p {partition} -w {busy[1]} -N1 -n1 --mem=1 -L {LICENSE}:1 -t 5 "
        '--wrap="sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        blocker, "RUNNING", timeout=JOB_START_TIMEOUT, poll_interval=POLL_INTERVAL
    ), "Verify that the license blocker job started"

    planned = probe_future_placement(partition)
    planned_busy = [name for name in planned if name in busy]

    if busy_nodes:
        assert busy[0] in planned, (
            f"Verify that bf_busy_nodes planned the job onto {busy[0]}, the "
            f"in-use node the busy pass reaches first, but got {planned}"
        )
    else:
        assert not planned_busy, (
            f"Verify that without bf_busy_nodes the same layout plans only on "
            f"idle nodes, expected a subset of {idle} but got {planned}"
        )
