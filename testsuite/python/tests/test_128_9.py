############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GraceTime + PreemptMode=REQUEUE under preempt/qos must not requeue twice.

This is the preempt/qos counterpart to test_128_8.py. Ticket 25682 fixed a
stale GRACE_PREEMPT bit that forces a second requeue when a restarted batch
script exits 0. The fix lives in job_requeue_internal, which both
preempt/partition_prio and preempt/qos route through, but QOS GraceTime is
resolved from the database (via slurmdbd) rather than from the partition
record, so a divergence in how the QOS path reaches the grace/requeue logic
would not be caught by the partition_prio test alone. This module exercises
the same sequence with a preemptable low-priority QOS instead of a
low-priority partition.

Requires Slurm 26.05+ (ticket 25682; the fix is backported to the 26.05 and
26.11 branches).

See test_128_8.py for the timing constants; they are duplicated here
rather than shared since the two modules configure preemption through
unrelated mechanisms (partitions vs. QOS) and have little else in common.
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

_QOS_LOW = "test_128_9_low"
_QOS_HIGH = "test_128_9_high"
_PARTITION = "test_128_9"


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
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("PreemptType", "preempt/qos")
    # Global PreemptMode must be non-OFF for preempt/qos to load, but it is
    # deliberately set to a different mode than the low QOS's PreemptMode
    # (REQUEUE, set below): if both were REQUEUE, a run where the cluster-wide
    # default governed instead of the QOS override would still pass every
    # assertion below, so this test wouldn't actually prove the QOS-resolved
    # PreemptMode took effect.
    atf.require_config_parameter("PreemptMode", "CANCEL")
    atf.require_config_parameter("PreemptExemptTime", "0")
    atf.require_config_parameter("JobRequeue", "1")
    atf.require_config_parameter("KillWait", str(_KILL_WAIT_SEC))
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    # Preemption in preempt/qos is governed by QOS, not partition, but the
    # node/partition relationship still must not be assumed: give both jobs
    # an explicit partition instead of relying on whatever the default
    # partition happens to be.
    atf.require_config_parameter("PartitionName", {_PARTITION: {"Nodes": "ALL"}})
    atf.require_slurm_running()

    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    su = atf.properties["slurm-user"]

    atf.run_command(
        f"sacctmgr -i add qos {_QOS_LOW} "
        f"PreemptMode=REQUEUE GraceTime={_GRACE_TIME_SEC}",
        user=su,
        fatal=True,
    )
    # "Preempt=" lists the QOSs *this* QOS can preempt, so it belongs on the
    # high-priority QOS, pointing at the low one.
    atf.run_command(
        f"sacctmgr -i add qos {_QOS_HIGH} Preempt={_QOS_LOW}",
        user=su,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} cluster={cluster} account=root",
        user=su,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos+={_QOS_LOW},{_QOS_HIGH}",
        user=su,
        fatal=True,
    )
    _wait_for_qos_grant(user, _QOS_LOW, _PARTITION)
    _wait_for_qos_grant(user, _QOS_HIGH, _PARTITION)

    yield

    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos-={_QOS_LOW},{_QOS_HIGH}",
        user=su,
        quiet=True,
    )
    atf.run_command(f"sacctmgr -i remove qos {_QOS_LOW}", user=su, quiet=True)
    atf.run_command(f"sacctmgr -i remove qos {_QOS_HIGH}", user=su, quiet=True)


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


def _wait_for_qos_grant(user, qos, partition):
    """Block until slurmctld has picked up a QOS grant made via sacctmgr.

    sacctmgr provisioning reaches slurmctld asynchronously through the
    assoc_mgr update pushed from slurmdbd, so a submit issued right after
    granting a QOS can race that update and be rejected with "Invalid qos
    specification". A trial sbatch --test-only exercises the exact
    acceptance check a real submit would hit, so success here means a real
    submit is safe.
    """

    for _ in atf.timer():
        result = atf.run_command(
            f"sbatch --test-only -p {partition} -q {qos} --wrap true",
            user=user,
            quiet=True,
        )
        if result["exit_code"] == 0:
            return
    pytest.fail(
        f"QOS '{qos}' was never accepted for user '{user}'; the sacctmgr "
        f"grant never propagated to slurmctld"
    )


def test_gracetime_requeue_clears_before_next_run():
    """After GraceTime+REQUEUE under preempt/qos, the restarted job must
    complete with Restarts=1.

    Sequence: low-QOS job runs, high-QOS job preempts it when GraceTime
    expires (job_requeue_internal), low job is requeued once, then exits 0
    when the node is free. Restarts must stay at 1 (not jump to 2 from a
    forced requeue of that successful run).
    """

    node = atf.run_job_nodes(
        f"-N1 -c2 -t1 --exclusive -p {_PARTITION} -q {_QOS_LOW} true", fatal=True
    )[0]

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
        f"-w {node} -c2 -t5 -o /dev/null --exclusive -p {_PARTITION} "
        f"-q {_QOS_LOW} --requeue {script}",
        fatal=True,
    )
    atf.wait_for_job_state(low_job, "RUNNING", fatal=True)

    high_job = atf.submit_job_sbatch(
        f"-w {node} -c2 -t1 -o /dev/null --exclusive -p {_PARTITION} "
        f"-q {_QOS_HIGH} --wrap 'sleep {_HIGH_WRAP_SLEEP_SEC}'",
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

    # The partition carries no GraceTime, so a grace window of exactly
    # _GRACE_TIME_SEC can only have come from the QOS. EndTime is clamped to
    # PreemptTime+GraceTime while the job is in grace, so the equality holds
    # only because TimeLimit exceeds GraceTime. Require EndTime > PreemptTime:
    # a sample taken once grace has already expired pairs a re-stamped
    # PreemptTime with a stale EndTime.
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
    # RUNNING. Either the job completes (correct) or that exit is turned into
    # a second requeue, which shows up as Restarts=2.
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
