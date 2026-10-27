############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that requeued array tasks don't flood slurmdbd with DBD_JOB_START.

Ticket 25801: requeued tasks of a throttled array are separate job records
that wait behind the array's task limit. Each time the limit gained room, every
one of them sent a DBD_JOB_START rewriting the same INFINITE eligible time, so
the message rate grew with the number of waiting tasks until the DBD agent
queue filled up.

Ticket 25946 then clears the JobArrayTaskLimit reason once the limit has room,
and that reason is what lets those redundant messages be skipped, so this also
guards that fix against bringing the flood back.

The array's own pending tasks always take the free slots ahead of the requeued
ones, which therefore keep waiting while the limit opens and closes.
"""

import logging
import re
import time

import pytest

import atf

_version = atf.get_version("sbin/slurmctld")
_NO_FLOOD_FIXED = _version >= (26, 5, 5) or (25, 11, 9) <= _version < (26, 5)
_NO_FLOOD = (
    "Ticket 25801: requeued array tasks stop resending INFINITE eligible "
    "times in 25.11.9 and 26.05.5"
)

THROTTLE = 2
REQUEUE_ROUNDS = 8
REQUEUE_DELAY = 1
# Long enough to requeue, and few starts so they don't hide the resends.
TASK_SECONDS = 20
# The resends follow scheduling passes, a few per minute for each waiting
# task, so a longer window separates them from the starts better.
WINDOW_SECONDS = 120

# Per task start: its own job start, the update of the array's pending
# record after the split, and one INFINITE eligible time when the limit
# fills again. On top of that, each waiting task may send one update.
MESSAGES_PER_START = 4
MESSAGES_PER_WAITING_TASK = 1


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Before requeue_delay, requeued tasks wait cred_expire (120s by default)
    # and can't resend anything during the window.
    atf.require_version(
        (25, 11),
        "sbin/slurmctld",
        reason="Needs SchedulerParameters=requeue_delay, added in 25.11",
    )
    atf.require_accounting()
    atf.require_nodes(1, [("CPUs", THROTTLE)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("requeue_delay", REQUEUE_DELAY)
    )
    atf.require_slurm_running()


def job_start_count():
    """Returns how many DBD_JOB_START messages slurmdbd has received."""
    output = atf.run_command_output(
        "sacctmgr show stats", user=atf.properties["slurm-user"], fatal=True
    )
    match = re.search(r"DBD_JOB_START\s*\(\s*\d+\)\s*count:(\d+)", output)
    if not match:
        pytest.fail(f"No DBD_JOB_START count in sacctmgr show stats:\n{output}")
    return int(match.group(1))


def task_ids(array_id, state):
    """Returns the job IDs of the array's tasks in a state, one per record."""
    output = atf.run_command_output(
        f"squeue -h -r -j {array_id} -t {state} -o %A", fatal=True
    )
    return set(output.split())


def started_tasks(array_id, since):
    """Returns how many of the array's tasks started at or after a time."""
    output = atf.run_command_output(
        f"sacct -n -X -P -D -j {array_id} -o Start", fatal=True
    )
    starts = [
        time.mktime(time.strptime(start, "%Y-%m-%dT%H:%M:%S"))
        for start in output.split()
        if start not in ("Unknown", "None")
    ]
    return sum(1 for start in starts if start >= since)


@pytest.mark.xfail(not _NO_FLOOD_FIXED, reason=_NO_FLOOD)
def test_no_job_start_flood():
    """Test that waiting requeued tasks don't resend their job start."""
    array_id = atf.submit_job_sbatch(
        f"-n1 --array=1-1000%{THROTTLE} -o /dev/null --wrap 'sleep {TASK_SECONDS}'",
        fatal=True,
    )

    # Build a pile of requeued tasks waiting behind the limit.
    for _ in range(REQUEUE_ROUNDS):
        for _ in atf.timer(fatal=True):
            running = task_ids(array_id, "R")
            if len(running) == THROTTLE:
                break
        atf.run_command(f"scontrol requeue {','.join(running)}", fatal=True)
        for _ in atf.timer(fatal=True):
            if not task_ids(array_id, "R") & running:
                break

    # Let each requeued task send its one-off update once it is eligible.
    time.sleep(REQUEUE_DELAY + TASK_SECONDS)
    waiting = task_ids(array_id, "PD") - {str(array_id)}
    assert len(waiting) >= REQUEUE_ROUNDS, (
        f"At least {REQUEUE_ROUNDS} requeued tasks should be waiting, "
        f"but only {sorted(waiting)} are"
    )

    window_start = time.time()
    messages_before = job_start_count()
    time.sleep(WINDOW_SECONDS)
    messages = job_start_count() - messages_before
    starts = started_tasks(array_id, window_start)
    logging.info(
        "%d DBD_JOB_START for %d task starts, %d requeued tasks waiting",
        messages,
        starts,
        len(waiting),
    )

    assert starts > 0, "The array should keep starting tasks"
    limit = MESSAGES_PER_START * starts + MESSAGES_PER_WAITING_TASK * len(waiting)
    assert messages <= limit, (
        f"{messages} DBD_JOB_START messages for {starts} task starts with "
        f"{len(waiting)} requeued tasks waiting; the waiting tasks should not "
        "resend their job start each time the limit has room"
    )
