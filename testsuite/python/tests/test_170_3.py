############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Backfill starts jobs on a nextstate=resume node that is still available.

A node waiting on a non-ASAP reboot with nextstate=resume can be allocated
until its reboot is issued, and backfill must start a job there when the node
can run it now. A node drained by reboot ASAP, or one whose reboot was already
issued, can not be allocated. Those cases, and the same node after a slurmctld
restart, check that a job still waits there.
"""

import pytest

import atf

node = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    # Only backfill may start the job: with defer, nothing starts a job at
    # submit, and sched_interval=-1 turns the main scheduler off. Use the
    # (key, value) form so a value the config already has is replaced; Slurm
    # honors the first occurrence of a key.
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", -1))
    atf.require_config_parameter_includes("SchedulerParameters", "defer")
    # Backfill plans a pending job, and sets SchedNodes, only at or above
    # bf_min_prio_reserve; the perf config raises it above our job's priority.
    # Use 1 rather than the default 0: the backfill parser logs an explicit 0
    # as invalid, and 1 only excludes held (priority 0) jobs, which we have none.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 1)
    )
    # Issue a requested reboot within a second, not on the 10 s power thread
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("power_save_interval", 1)
    )
    # The holder and the job share the node, which needs cons_tres; CR_CPU is
    # not valid with select/linear
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("RebootProgram", "/bin/true")
    # A node whose reboot was issued must not time out during the test.
    atf.require_config_parameter("ResumeTimeout", 600)
    nodes = atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter(
        "PartitionName",
        {"part1": {"Nodes": nodes[0], "Default": "YES", "MaxTime": "INFINITE"}},
    )
    atf.require_slurm_running()

    global node
    node = nodes[0]


@pytest.fixture(scope="function", autouse=True)
def reset_node():
    """Cancel any pending reboot and jobs, and return the node to service."""
    yield
    # Cancel the reboot first: once the jobs are gone, an idle node with a
    # pending reboot has it issued, and cancel_reboot then fails.
    atf.run_command(
        f"scontrol cancel_reboot {node}", user=atf.properties["slurm-user"], quiet=True
    )
    atf.cancel_all_jobs(quiet=True)
    atf.run_command(
        f"scontrol update nodename={node} state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    # Exact match: IDLE together with NOT_RESPONDING is not allocable yet
    atf.wait_for_node_state(node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)


def submit_holder():
    """Run a one CPU job so the node is MIXED, with a CPU left free."""
    job_id = atf.submit_job_sbatch(
        f"-J holder -N1 -n1 -w {node} --time=10 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


def submit_pack():
    """Submit a one CPU job that must run on the node."""
    return atf.submit_job_sbatch(
        f"-J pack -N1 -n1 -w {node} --time=2 --wrap 'sleep 30'", fatal=True
    )


def job_snapshot(job_id):
    """Return a job's state, backfill-planned nodes and last scheduling
    evaluation time from one scontrol call, so a start cannot fall between
    two reads."""
    job = atf.get_jobs(job_id=job_id, quiet=True)[job_id]
    return (
        job["JobState"],
        job.get("SchedNodeList", ""),
        job["LastSchedEval"],
    )


def check_waits_on_node(job_id, node_flag):
    """Check backfill evaluated a job on the node and the job still waits.

    Backfill sets SchedNodes in the pass that evaluates the job, both when it
    postpones the start and when select_nodes() refuses it, so this does not
    tell the two apart. A postponed job is postponed again in every pass, so
    wait for a later pass, which advances LastSchedEval, and check the job
    still waits. The state and nodes come from one scontrol call, so a start
    cannot fall between two reads.
    """
    # Poll faster than the timer default so a pass is seen soon after it runs
    for _ in atf.timer(poll_interval=0.2, fatal=True):
        first = job_snapshot(job_id)
        if first[0] != "PENDING" or first[1] == node:
            break
    assert first[0] == "PENDING", f"The job should wait, not be {first[0]}"

    for _ in atf.timer(poll_interval=0.2, fatal=True):
        state, planned, last_eval = job_snapshot(job_id)
        if state != "PENDING" or last_eval != first[2]:
            break
    assert state == "PENDING", f"The job should still wait, not be {state}"
    assert planned == node, "Backfill should plan the job on the resuming node"
    assert node_flag in atf.get_node_parameter(
        node, "state"
    ), f"The node should still be {node_flag}"


# Releases before 26.11 postpone every backfill start on a resuming node.
@pytest.mark.xfail(
    atf.get_version() < (26, 11),
    reason="Ticket 25233: backfill treats an available resuming node as future-only",
)
def test_starts_on_available_resuming_node():
    """A job starts on a MIXED node waiting on a non-ASAP reboot."""

    submit_holder()
    atf.run_command(
        f"scontrol reboot nextstate=resume {node}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node, "REBOOT_REQUESTED", fatal=True)
    assert "RESUME" in atf.get_node_parameter(
        node, "next_state_after_reboot", default=[]
    ), "The node should be a resuming node"

    job_id = submit_pack()

    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Backfill should start the job on the available resuming node"
    assert "REBOOT_REQUESTED" in atf.get_node_parameter(
        node, "state"
    ), "The reboot request should still be pending"
    assert "RESUME" in atf.get_node_parameter(
        node, "next_state_after_reboot", default=[]
    ), "The node should still be a resuming node"

    # The job deferred the reboot; it must still be issued once the node is idle
    atf.cancel_all_jobs(quiet=True, fatal=True)
    atf.wait_for_node_state(node, "REBOOT_ISSUED", fatal=True)


def test_asap_reboot_node_stays_unavailable():
    """A job does not start on a node drained by reboot ASAP.

    select_nodes() also refuses the node, so this cannot tell a backfill
    postpone from a refused start.
    """

    submit_holder()
    atf.run_command(
        f"scontrol reboot ASAP nextstate=resume {node}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node, "DRAIN", fatal=True)

    job_id = submit_pack()

    check_waits_on_node(job_id, "DRAIN")


def check_waits(job_id, node_flag):
    """Check a job still waits after a later backfill pass, on a node that
    backfill does not plan on, so SchedNodes stays empty."""
    first = job_snapshot(job_id)
    assert first[0] == "PENDING", f"The job should wait, not be {first[0]}"

    for _ in atf.timer(poll_interval=0.2, fatal=True):
        state, _, last_eval = job_snapshot(job_id)
        if state != "PENDING" or last_eval != first[2]:
            break
    assert state == "PENDING", f"The job should still wait, not be {state}"
    assert node_flag in atf.get_node_parameter(
        node, "state"
    ), f"The node should still be {node_flag}"


@pytest.mark.parametrize("restart", [False, True])
@pytest.mark.parametrize("nextstate", ["nextstate=resume", ""])
def test_reboot_issued_node_stays_unavailable(nextstate, restart):
    """A job does not start on a node whose reboot was already issued.

    Without a restart, select_nodes() also refuses the node, so this cannot
    tell a backfill postpone from a refused start. The restart case covers the
    controller coming back up with the node in REBOOT_ISSUED, where the node
    must not be made available again, with and without nextstate=resume. A
    node without nextstate is not a resuming node, so backfill does not plan
    on it and only the wait is checked.
    """

    atf.run_command(
        f"scontrol reboot {nextstate} {node}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node, "REBOOT_ISSUED", fatal=True)

    if restart:
        atf.restart_slurmctld()
        atf.wait_for_node_state(node, "REBOOT_ISSUED", fatal=True)

    job_id = submit_pack()

    if nextstate:
        check_waits_on_node(job_id, "REBOOT_ISSUED")
    else:
        check_waits(job_id, "REBOOT_ISSUED")
