############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GraceTime + PreemptMode=REQUEUE must not requeue a later successful run.

With partition-priority preemption, a low-priority job that is preempted after
a nonzero GraceTime is correctly requeued once. After the high-priority job
finishes and the low-priority job restarts, that restarted run must complete
normally (Restarts=1). A stale GRACE_PREEMPT bit incorrectly forces a second
requeue when the restarted batch script exits 0.

Requires Slurm 26.05+ (ticket 25682; the fix is backported to the 26.05 and
26.11 branches).

A batch-only victim (no srun step) is not killed by GraceTime's warning
signal, so this test waits for GraceTime expiry (job_requeue_internal).
PreemptTime and EndTime pin that the job entered grace (EndTime is
clamped to PreemptTime+GraceTime) rather than inferring it from a short
"still RUNNING" window.

GraceTime expiry may be acted on promptly by the scheduler, but in the worst
case it waits for the controller's periodic time limit check
(PERIODIC_TIMEOUT, 30 seconds).

Requires: PreemptType=preempt/partition_prio, overlapping PriorityTier
partitions on one node, GraceTime>0 and PreemptMode=REQUEUE on the low
partition, JobRequeue enabled, and SchedulerParameters requeue_delay=0 plus
tight bf_interval/sched_interval so post-requeue starts are prompt. Both jobs
are pinned to the same node (-w) so a multi-node cluster cannot let the
high-priority job simply start elsewhere instead of preempting.

The bug's user-visible symptom, per ticket 25682, is the restarted job's
normal exit being recorded as PREEMPTED in accounting. Live scontrol state
can settle correctly while that database record is still wrong, so the test
also checks sacct -D for the low-priority job: one PREEMPTED/REQUEUED record
for the interrupted run, and a final COMPLETED record for the restarted run.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

_GRACE_TIME_SEC = 15
_GRACE_STILL_RUNNING_SEC = 5
_HIGH_WRAP_SLEEP_SEC = 5

# Worst-case GraceTime expiry waits for PERIODIC_TIMEOUT. After requeue the
# job stays COMPLETING until KillWait, so keep KillWait short.
_PERIODIC_TIMEOUT_SEC = 30
_KILL_WAIT_SEC = 5
_REQUEUE_TIMEOUT_SEC = _GRACE_TIME_SEC + _PERIODIC_TIMEOUT_SEC + _KILL_WAIT_SEC + 40


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmctld",
        reason=(
            "Ticket 25682: GRACE_PREEMPT was left set after a preempt requeue, "
            "forcing a second requeue on a successful run. Fixed on master and "
            "backported to 26.05 and 26.11."
        ),
    )
    atf.require_accounting()
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "REQUEUE")
    atf.require_config_parameter("PreemptExemptTime", "0")
    atf.require_config_parameter("JobRequeue", "1")
    atf.require_config_parameter("KillWait", str(_KILL_WAIT_SEC))
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter(
        "PartitionName",
        {
            "lowprio": {
                "Nodes": "ALL",
                "PriorityTier": "1",
                "GraceTime": str(_GRACE_TIME_SEC),
                "PreemptMode": "REQUEUE",
            },
            "highprio": {
                "Nodes": "ALL",
                "PriorityTier": "2",
            },
        },
    )
    atf.require_slurm_running()


def _sacct_records(job_id):
    output = atf.run_command_output(
        f"sacct -j {job_id} -D -X -o State,Restarts -P -n", fatal=True
    )
    records = [line.split("|") for line in output.splitlines() if line]
    # sacct's output order for duplicate records of one job id is not
    # guaranteed, so sort by Restarts to reliably tell the interrupted run's
    # record from the restarted run's record.
    records.sort(key=lambda r: int(r[1] or 0))
    return records


def _wait_for_sacct_completed(job_id):
    records = None
    for _ in atf.timer():
        records = _sacct_records(job_id)
        if records and records[-1][0] == "COMPLETED":
            break
    return records


def _assert_two_run_sacct_history(job_id, records):
    """Confirm one PREEMPTED/REQUEUED record then a final COMPLETED record.

    Live scontrol state can settle correctly while the accounting record for
    the restarted run is still stamped PREEMPTED (the user-visible symptom of
    ticket 25682), so this checks what actually landed in the database.
    """

    assert len(records) == 2, (
        f"Job ({job_id}) should have exactly two accounting records "
        f"(interrupted run + restarted run), got {records}"
    )
    assert (
        records[-1][0] == "COMPLETED"
    ), f"Job ({job_id}) final accounting record should be COMPLETED, got {records}"
    assert records[0][0] in ("PREEMPTED", "REQUEUED"), (
        f"Job ({job_id}) first accounting record should be PREEMPTED or "
        f"REQUEUED (the GraceTime requeue), got {records}"
    )


def test_gracetime_requeue_clears_before_next_run():
    """After GraceTime+REQUEUE, the restarted job must complete with Restarts=1.

    Sequence: low-priority job runs, high-priority job preempts it when
    GraceTime expires (job_requeue_internal), low job is requeued once, then
    exits 0 when the node is free. Restarts must stay at 1 (not jump to 2
    from a forced requeue of that successful run).

    The restarted run exits immediately, so success is observed as the terminal
    COMPLETED state with Restarts=1 rather than by catching it RUNNING.
    """

    node = atf.run_job_nodes("-N1 -c2 -t1 --exclusive -p lowprio true", fatal=True)[0]

    script = "low_preempt.sh"
    atf.make_bash_script(
        script,
        """
if [ "${SLURM_RESTART_COUNT:-0}" -eq 0 ]; then
    sleep infinity
fi
exit 0
""",
    )

    low_job = atf.submit_job_sbatch(
        f"-w {node} -c2 -t5 -o /dev/null --exclusive -p lowprio --requeue {script}",
        fatal=True,
    )
    atf.wait_for_job_state(low_job, "RUNNING", fatal=True)

    high_job = atf.submit_job_sbatch(
        f"-w {node} -c2 -t1 -o /dev/null --exclusive -p highprio "
        f"--wrap 'sleep {_HIGH_WRAP_SLEEP_SEC}'",
        fatal=True,
    )

    # Still inside GraceTime: must remain RUNNING.
    assert not atf.wait_for_job_state(
        low_job,
        "PENDING",
        timeout=_GRACE_STILL_RUNNING_SEC,
        xfail=True,
    ), (
        f"Low-priority job ({low_job}) left RUNNING before GraceTime expired; "
        f"requeue must come from GraceTime expiry"
    )
    assert (
        atf.get_job_parameter(low_job, "JobState", quiet=True) == "RUNNING"
    ), f"Low-priority job ({low_job}) should still be RUNNING during GraceTime"

    # EndTime is clamped to PreemptTime+GraceTime while the job is in grace,
    # so the equality below holds only because TimeLimit exceeds GraceTime.
    # Require EndTime > PreemptTime: a sample taken once grace has already
    # expired pairs a re-stamped PreemptTime with a stale EndTime.
    preempt_time = end_time = None
    for _ in atf.timer():
        job = atf.get_jobs(low_job, use_json=True, quiet=True)[low_job]
        preempt_time = job["preempt_time"]
        end_time = job["end_time"]
        if (
            preempt_time["set"]
            and end_time["set"]
            and end_time["number"] > preempt_time["number"]
        ):
            break
    assert preempt_time["set"], (
        f"Low-priority job ({low_job}) never got a PreemptTime set after the "
        f"high-priority job ({high_job}) was submitted"
    )
    assert end_time["number"] - preempt_time["number"] == _GRACE_TIME_SEC, (
        f"Low-priority job ({low_job}) EndTime ({end_time['number']}) should "
        f"be exactly GraceTime ({_GRACE_TIME_SEC}s) after PreemptTime "
        f"({preempt_time['number']})"
    )

    # GraceTime expiry requeue (job_requeue_internal), not in-grace completion.
    assert atf.wait_for_job_state(
        low_job,
        "PENDING",
        timeout=_REQUEUE_TIMEOUT_SEC,
    ), f"Low-priority job ({low_job}) was not requeued after GraceTime"
    restarts = atf.get_job_parameter(low_job, "Restarts", default=0, quiet=True)
    assert restarts == 1, (
        f"Low-priority job ({low_job}) should have Restarts=1 after the "
        f"preemption requeue, got {restarts}"
    )

    atf.wait_for_job_state(high_job, "DONE", timeout=60, fatal=True)

    # The restarted run exits 0 immediately, so it is never observable as
    # RUNNING. Either the job completes (correct) or that exit is turned into a
    # second requeue, which shows up as Restarts=2.
    state = restarts = None
    for _ in atf.timer(timeout=60):
        state = atf.get_job_parameter(low_job, "JobState", quiet=True)
        restarts = atf.get_job_parameter(low_job, "Restarts", default=0, quiet=True)
        if state == "COMPLETED" or restarts >= 2:
            break

    assert restarts == 1, (
        f"Low-priority job ({low_job}) should complete with Restarts=1 after "
        f"one GraceTime+REQUEUE preemption; Restarts={restarts} means a later "
        f"successful run was treated as another preemption requeue"
    )
    assert state == "COMPLETED", (
        f"Low-priority job ({low_job}) should be COMPLETED after the "
        f"post-preempt run, got {state} Restarts={restarts}"
    )

    _assert_two_run_sacct_history(low_job, _wait_for_sacct_completed(low_job))
