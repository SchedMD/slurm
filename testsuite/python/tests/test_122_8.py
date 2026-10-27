############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a pending job array drops JobArrayTaskLimit once its limit has room.

Ticket 25946: after the running task of an array throttled to one task ended,
the remaining tasks could show Reason=JobArrayTaskLimit for as long as they
kept waiting. For a job it skips behind a higher priority job, the main
scheduler only replaces a reason of None or Resources, and backfill does not
set reasons, so nothing replaced the stale one.

Here the array's running task is cancelled while a higher priority job waits
for the whole partition. The next task then waits behind that job, and has to
show Reason=Priority.
"""

import pytest

import atf

_STALE_REASON_FIXED = atf.get_version("sbin/slurmctld") >= (26, 11)
_STALE_REASON = (
    "Ticket 25946: pending array tasks drop JobArrayTaskLimit once the limit "
    "has room in 26.11"
)

# The filler's time limit is shorter than the array tasks', so backfill can't
# start the next task without delaying the higher priority job.
FILLER_MINUTES = 10
TASK_MINUTES = 30


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    # Below this priority, neither scheduler holds resources for the higher
    # priority job, so the next task would start instead of waiting.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 0)
    )
    atf.require_slurm_running()


def pending_reason(array_id):
    """Returns the reason of the array's pending tasks."""
    return atf.run_command_output(
        f"squeue -h -j {array_id} -t PD -o %r", fatal=True
    ).strip()


def running_task(array_id):
    """Returns the job ID of the array's running task, if any."""
    return atf.run_command_output(
        f"squeue -h -j {array_id} -t R -o %A", fatal=True
    ).strip()


@pytest.mark.xfail(not _STALE_REASON_FIXED, reason=_STALE_REASON)
def test_reason_after_limit_has_room():
    """Test that the next task waits for Priority, not the array task limit."""
    partition = atf.default_partition()
    node_count = sum(
        int(count)
        for count in atf.run_command_output(
            f"sinfo -h -p {partition} -o %D", fatal=True
        ).split()
    )

    filler_id = atf.submit_job_sbatch(
        f"-p {partition} -n1 -t {FILLER_MINUTES} -o /dev/null "
        "--wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(filler_id, "RUNNING", fatal=True)

    array_id = atf.submit_job_sbatch(
        f"-p {partition} -n1 -t {TASK_MINUTES} --array=1-2%1 -o /dev/null "
        "--wrap 'sleep infinity'",
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        task_id = running_task(array_id)
        if task_id:
            break
    for _ in atf.timer(fatal=True):
        if pending_reason(array_id) == "JobArrayTaskLimit":
            break

    # Needs every node, so it waits for the filler and fails the partition
    # for the jobs below it.
    blocker_id = atf.submit_job_sbatch(
        f"-p {partition} -N {node_count} --exclusive -t 1 -o /dev/null "
        "--wrap 'sleep 1'",
        fatal=True,
    )
    array_priority = int(atf.get_job_parameter(array_id, "Priority", fatal=True))
    atf.run_command(
        f"scontrol update JobId={blocker_id} Priority={array_priority + 1000}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(blocker_id, "PENDING", "Resources", fatal=True)

    atf.run_command(f"scancel {task_id}", fatal=True)
    atf.wait_for_job_state(int(task_id), "CANCELLED", fatal=True)

    reason = None
    for _ in atf.timer(timeout=90):
        reason = pending_reason(array_id)
        if reason == "Priority":
            break
    assert (
        running_task(array_id) == ""
    ), "The next task should still wait behind the higher priority job"
    assert reason == "Priority", (
        "The next task should wait for Priority once the limit has room, "
        f"but shows {reason!r}"
    )
