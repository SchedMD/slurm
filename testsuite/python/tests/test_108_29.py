############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""The node list of a pending job across rejected and accepted updates.

A job's required node list fixes where it can run once its node counts are
final: a list longer than --nodes means "allocate from this list", a list
as long as a fixed --nodes is the whole allocation. Every update must leave
the job able to run where its current list and node count allow:

- a rejected update must leave the job as submitted. Ticket 25865: a
  rejected update left a job with a list longer than --nodes with the whole
  list required, and it was never scheduled again;
- an accepted update of the list or the node count must let the job run
  where the new list and count allow, keeping nothing of the old list.

The same must hold for a job recovered by a slurmctld restart, which
rebuilds the node lists on recovery: with no update at all, and with an
update made after the restart.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

_BLOCKED_REASONS = ("Resources", "Priority")
# A negative wait only needs a few scheduler cycles (sched_interval=1)
_NEGATIVE_WAIT_SEC = 5
_NODE_FIELDS = ("ReqNodeList", "ExcNodeList", "NumNodes")


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25865: node list exclusion kept across failed updates in 26.11",
    )
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_nodes(2, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes():
    """(node_a, node_b, exclude): two test nodes and -x for the rest.

    require_nodes() guarantees at least two nodes. A job whose node list is
    cleared or grown could run on any further node instead, so the jobs
    below exclude the others.
    """

    pair = atf.run_job_nodes("-N2 true", fatal=True)
    assert len(pair) == 2, f"Expected two allocated nodes, got {pair}"
    others = sorted(set(atf.get_nodes(quiet=True)) - set(pair))
    exclude = f"-x {','.join(others)}" if others else ""
    return pair[0], pair[1], exclude


def _occupy(node):
    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node} --exclusive -t5 -o /dev/null --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


def _submit(args):
    return atf.submit_job_sbatch(
        f"{args} --exclusive -t1 -o /dev/null --wrap 'sleep infinity'",
        fatal=True,
    )


def _assert_pending_blocked(job):
    """Check that the job stays pending for lack of nodes over a few cycles."""

    assert not atf.wait_for_job_state(
        job, "RUNNING", timeout=_NEGATIVE_WAIT_SEC, xfail=True
    ), f"Job ({job}) should pend while the nodes it may use are busy"
    state = atf.get_job_parameter(job, "JobState", quiet=True)
    assert state == "PENDING", f"Job ({job}) should be pending, is {state}"
    reason = atf.get_job_parameter(job, "Reason", quiet=True)
    assert reason in _BLOCKED_REASONS, f"Job ({job}) is pending for {reason}"


def _assert_runs_on(job, expected):
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), f"Job ({job}) never started; it must be able to run on {expected}"
    allocated = atf.node_range_to_list(
        atf.get_job_parameter(job, "NodeList", quiet=True)
    )
    assert sorted(allocated) == sorted(
        expected
    ), f"Job ({job}) should run on {expected}, got {allocated}"


@pytest.mark.parametrize(
    "whole_list", [False, True], ids=["list_longer_than_count", "list_is_count"]
)
def test_node_list_kept_after_restart(nodes, whole_list):
    """A job recovered by a restart keeps the meaning of its node list.

    -N1 -w node_a,node_b must still run on node_b once it is freed, and
    -N1 -w node_a must stay off the free node_b, with no update in between.
    """

    node_a, node_b, exclude = nodes
    occupant_a = _occupy(node_a)
    if whole_list:
        job = _submit(f"-N1 -w {node_a} {exclude}")
    else:
        occupant_b = _occupy(node_b)
        job = _submit(f"-N1 -w {node_a},{node_b} {exclude}")
    _assert_pending_blocked(job)

    atf.restart_slurmctld()
    _assert_pending_blocked(job)

    if whole_list:
        atf.cancel_jobs([occupant_a], fatal=True)
        _assert_runs_on(job, [node_a])
    else:
        atf.cancel_jobs([occupant_b], fatal=True)
        _assert_runs_on(job, [node_b])


@pytest.mark.parametrize("restart", [False, True], ids=["no_restart", "restart"])
@pytest.mark.parametrize(
    "bad_update",
    [
        "ExcNodeList=nosuchnode",
        "ReqNodeList=nosuchnode",
        "NumNodes=0",
        # Rejected before the node bitmaps are rebuilt: the translation
        # applied at the end of every update must leave them as they were
        "Partition=nosuchpart",
    ],
    ids=["exc_nodes", "req_nodes", "node_count", "partition"],
)
def test_node_list_kept_after_rejected_update(nodes, restart, bad_update):
    """-N1 -w node_a,node_b still runs on node_b after a rejected update."""

    node_a, node_b, exclude = nodes
    _occupy(node_a)
    occupant_b = _occupy(node_b)
    job = _submit(f"-N1 -w {node_a},{node_b} {exclude}")
    _assert_pending_blocked(job)

    if restart:
        atf.restart_slurmctld()
        _assert_pending_blocked(job)
    # Taken after the restart: NumNodes is displayed as "1-1" or "1" depending
    # on whether the scheduler has estimated the node count yet, which a
    # restart resets. The comparison is about the rejected update only.
    before = {f: atf.get_job_parameter(job, f, quiet=True) for f in _NODE_FIELDS}

    result = atf.run_command(
        f"scontrol update JobId={job} {bad_update}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert result["exit_code"] != 0, f"Update '{bad_update}' should be rejected"
    assert (
        "invalid" in result["stderr"].lower()
    ), f"Update '{bad_update}' should be rejected as invalid: {result}"
    after = {f: atf.get_job_parameter(job, f, quiet=True) for f in _NODE_FIELDS}
    assert after == before, (
        f"Job ({job}) node fields changed after the rejected update: "
        f"{before} -> {after}"
    )

    atf.cancel_jobs([occupant_b], fatal=True)
    _assert_runs_on(job, [node_b])


@pytest.mark.parametrize("restart", [False, True], ids=["no_restart", "restart"])
@pytest.mark.parametrize(
    "submit,update,expected",
    [
        ("-N1 -w {node_a}", "ReqNodeList={node_b}", ["node_b"]),
        ("-N1 -w {node_a}", "ReqNodeList=", ["node_b"]),
        ("-N1 -w {node_a}", "NumNodes=2", ["node_a", "node_b"]),
        ("-N1 -w {node_a}", "ReqNodeList={node_a},{node_b}", ["node_b"]),
        ("-N2 -w {node_a},{node_b}", "NumNodes=1", ["node_b"]),
    ],
    ids=["req_node_b", "req_cleared", "two_nodes", "req_grown", "count_shrunk"],
)
def test_node_list_rederived_after_accepted_update(
    nodes, restart, submit, update, expected
):
    """A pending job runs where an accepted update sends it.

    While node_a is busy the job pends, kept off node_b by its node list or
    by needing both nodes. After the update it must run on node_b (node_a
    is still busy) or, with two nodes, on both once node_a is freed.
    """

    node_a, node_b, exclude = nodes
    names = {"node_a": node_a, "node_b": node_b}
    submit = submit.format(**names)
    update = update.format(**names)
    expected = [names[name] for name in expected]
    occupant_a = _occupy(node_a)
    job = _submit(f"{submit} {exclude}")
    _assert_pending_blocked(job)

    if restart:
        atf.restart_slurmctld()
        _assert_pending_blocked(job)
    atf.run_command(
        f"scontrol update JobId={job} {update}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    if node_a in expected:
        atf.cancel_jobs([occupant_a], fatal=True)
    _assert_runs_on(job, expected)
