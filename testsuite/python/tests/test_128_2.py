############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
from datetime import datetime, timedelta

import pytest

import atf

pytestmark = pytest.mark.slow

_QOS_LOW = "test_128_2_low"
_QOS_HIGH = "test_128_2_high"
_PART_LOW = "lowprio"
_PART_EXEMPT = "exempt"
_PART_HIGH = "highprio"

# Used as the global PreemptExemptTime for preempt/partition_prio and as the
# QOS PreemptExemptTime for preempt/qos, so both cases wait the same time
_EXEMPT_TIME = timedelta(seconds=10)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "SUSPEND,GANG")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # The planned start time is only checked if backfill plans a pending job,
    # which it does only at or above bf_min_prio_reserve; the perf config
    # raises it above our job's priority. Use 1 rather than the default 0: the
    # backfill parser logs an explicit 0 as invalid, and 1 only excludes held
    # (priority 0) jobs, which we have none.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 1)
    )
    atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter("DefMemPerNode", "128")
    atf.require_config_parameter(
        "PartitionName",
        {
            _PART_LOW: {"Nodes": "ALL", "PriorityTier": "1", "Default": "YES"},
            _PART_EXEMPT: {"Nodes": "ALL", "PriorityTier": "1", "PreemptMode": "OFF"},
            _PART_HIGH: {"Nodes": "ALL", "PriorityTier": "2"},
        },
    )

    atf.require_version((26, 5), "sbin/slurmctld")
    atf.require_slurm_running()

    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    su = atf.properties["slurm-user"]

    atf.run_command(
        f"sacctmgr -i add qos {_QOS_LOW} PreemptExemptTime={_EXEMPT_TIME}",
        user=su,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add qos {_QOS_HIGH} Preempt={_QOS_LOW}", user=su, fatal=True
    )

    atf.run_command(
        f"sacctmgr -i add user {user} cluster={cluster} account=root ",
        user=su,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} account=root "
        f"set qos=normal,{_QOS_LOW},{_QOS_HIGH}",
        user=su,
        fatal=True,
    )
    _wait_for_qos_grant(user, _QOS_LOW, _PART_LOW)
    _wait_for_qos_grant(user, _QOS_HIGH, _PART_LOW)

    yield

    atf.run_command(
        f"sacctmgr -i remove user {user} where cluster={cluster} account=root",
        user=su,
        quiet=True,
    )
    atf.run_command(f"sacctmgr -i remove qos {_QOS_LOW}", user=su, quiet=True)
    atf.run_command(f"sacctmgr -i remove qos {_QOS_HIGH}", user=su, quiet=True)


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


def _parse_time(time_str: str | None) -> datetime | None:
    """Parse a string into a datetime object, or into None if invalid

    Args:
        time_str (str | None): String that may be a valid datetime in ISO format

    Returns:
        datetime | None: datetime object if parsed successfully, or None on ValueError
    """
    if time_str is None:
        return None
    elif time_str == "Unknown":
        return None
    else:
        try:
            result = datetime.fromisoformat(time_str)
            return result
        except ValueError:
            return None


def _compare_times(
    datetime1: datetime,
    datetime2: datetime,
    expected_delta: timedelta,
    margin: timedelta,
) -> bool:
    """Test whether two times are close to an expected delta.

    Args:
        datetime1 (datetime): Smaller of the two times, in datetime object
        datetime2 (datetime): Larger of the two times, in datetime object
        expected_delta (timedelta): Expected delta between the two times
        margin (timedelta): Margin by which the real delta may deviate from expected

    Returns:
        bool: Whether the two times are close to the expected delta
    """
    real_delta = datetime2 - datetime1
    early_enough = real_delta < (expected_delta + margin)
    late_enough = real_delta > (expected_delta - margin)
    return early_enough and late_enough


@pytest.mark.parametrize(
    "preempt_mode,preempted_state",
    [
        ("SUSPEND,GANG", "SUSPENDED"),
        ("REQUEUE", "PENDING"),
        ("CANCEL", "PREEMPTED"),
    ],
)
@pytest.mark.parametrize("preempt_type", ["preempt/partition_prio", "preempt/qos"])
def test_preempt_exempt_time(preempt_type, preempt_mode, preempted_state):
    """Verify that PreemptExemptTime protects a job across preemption modes.

    For each PreemptMode (SUSPEND,GANG / REQUEUE / CANCEL), submit a
    low-priority job, then a high-priority job on the same node.  The
    low-priority job should remain running during the exempt window and only
    be preempted after it expires. The window is the global PreemptExemptTime
    for preempt/partition_prio, and the QOS PreemptExemptTime for preempt/qos,
    where no global value is set. Both are the same length.
    """

    submit_low = f"-p {_PART_LOW}"
    submit_high = f"-p {_PART_HIGH}"

    if preempt_type == "preempt/qos":
        # Still submit to different partitions due to details of QOS-suspend,
        # which will only properly preempt when in different partitions.
        # Use exempt low-priority partition to verify QOS allows preemption.
        submit_low = f"-p {_PART_EXEMPT} -q {_QOS_LOW}"
        submit_high = f"-p {_PART_HIGH} -q {_QOS_HIGH}"

    # preempt/qos relies on the QOS PreemptExemptTime alone, so unset any
    # global value left over from an earlier case
    atf.set_config_parameter(
        "PreemptExemptTime",
        None if preempt_type == "preempt/qos" else str(_EXEMPT_TIME),
    )
    atf.set_config_parameter("PreemptType", preempt_type)
    atf.set_config_parameter("PreemptMode", preempt_mode, restart=True)

    job_id1 = atf.submit_job_sbatch(
        f'-c2 -o /dev/null {submit_low} -t 10 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id1, "RUNNING", timeout=10
    ), f"Low-priority job ({job_id1}) did not start"
    # The job started at or just before this moment, so the exempt window ends
    # no later than window_start + _EXEMPT_TIME
    window_start = datetime.now()

    job_id2 = atf.submit_job_sbatch(
        f'-c2 -o /dev/null {submit_high} -t 1 --wrap "sleep 20"',
        fatal=True,
    )

    # Wait for the high priority job to get an estimated StartTime which
    # confirms that backfill has tried planning it.
    # Keep this poll short: it must finish well inside the exempt window.
    for _ in atf.timer(poll_interval=0.5, timeout=5):
        planned_str = atf.get_job_parameter(job_id2, "StartTime", fatal=True)
        planned_dt = _parse_time(planned_str)
        if planned_dt is not None:
            break
    else:
        pytest.fail(
            f"Could not get valid high-priority job ({job_id2}) "
            f"attribute 'StartTime' (received {planned_str})"
        )

    assert (
        atf.get_job_parameter(job_id1, "JobState") == "RUNNING"
    ), f"Low-priority job ({job_id1}) should still be running during exempt window"

    # The preemptor must not have started either. Under SUSPEND,GANG the way
    # the exemption fails is that the preemptor is placed alongside the exempt
    # job on its cores, which leaves the victim RUNNING and never SUSPENDED --
    # indistinguishable from a working exemption unless the preemptor is
    # checked too.
    assert (
        atf.get_job_parameter(job_id2, "JobState") == "PENDING"
    ), f"High-priority job ({job_id2}) started during the exempt window"

    start_str = atf.get_job_parameter(job_id1, "StartTime", fatal=True)
    eligible_str = atf.get_job_parameter(job_id1, "PreemptEligibleTime", fatal=True)
    start_dt = _parse_time(start_str)
    eligible_dt = _parse_time(eligible_str)

    assert start_dt is not None and eligible_dt is not None, (
        f"Could not get valid low-priority job ({job_id1}) attributes "
        f"'StartTime' and/or 'PreemptEligibleTime' (received {start_str} "
        f"and {eligible_str})"
    )

    assert _compare_times(
        start_dt,
        eligible_dt,
        expected_delta=_EXEMPT_TIME,
        margin=timedelta(seconds=1),
    ), (
        f"Low-priority job ({job_id1}) PreemptEligibleTime ({eligible_str}) "
        f"does not match expected value ({start_str} + {_EXEMPT_TIME})"
    )

    # Ticket 23101: added planning for future preemption
    if atf.get_version("sbin/slurmctld") >= (26, 11):
        # High-priority job should be planned for the end of the exempt window
        assert _compare_times(
            eligible_dt,
            planned_dt,
            expected_delta=timedelta(0),
            margin=timedelta(seconds=1),
        ), (
            f"High-priority job ({job_id2}) was not planned near end of "
            f"exempt window (planned {planned_str}, expected {eligible_str})"
        )

    # Timeout to wait for the low-priority job to be preempted. Give an extra
    # 5 seconds after PreemptExemptTime for slower systems
    expiry_timeout = (
        _EXEMPT_TIME - (datetime.now() - window_start)
    ).total_seconds() + 5

    # After PreemptExemptTime expires the low-priority job must be preempted
    assert atf.wait_for_job_state(job_id1, preempted_state, timeout=expiry_timeout), (
        f"Low-priority job ({job_id1}) was not preempted ({preempted_state}) "
        f"after exempt time expired"
    )

    # High-priority job should now be running
    assert atf.wait_for_job_state(
        job_id2, "RUNNING", timeout=10
    ), f"High-priority job ({job_id2}) did not start after preemption"

    atf.cancel_jobs([job_id1, job_id2], fatal=True)


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Ticket 23654: exempt cores were rebuilt at most once a second, "
    "leaving a window at job start, until 26.11",
)
def test_preempt_exempt_time_covers_job_start():
    """A job is exempt from the moment it starts holding its allocation.

    test_preempt_exempt_time lets the low-priority job settle before the
    preemptor is submitted. That hides a window at job start: a preemptor
    scheduled in the same moment could be placed on the new job's cores before
    they were protected, and the victim stays RUNNING and unsuspended
    throughout, so only the preemptor shows it.

    Submit the preemptor as soon as the low-priority job is running, and repeat,
    so a window that only opens around job start is exercised rather than
    stepped over.
    """

    atf.set_config_parameter("PreemptType", "preempt/partition_prio")
    atf.set_config_parameter("PreemptMode", "SUSPEND,GANG", restart=True)
    atf.set_config_parameter("PreemptExemptTime", "00:00:10")

    for attempt in range(3):
        job_id1 = atf.submit_job_sbatch(
            '-c2 -o /dev/null -p lowprio --wrap "sleep infinity"',
            fatal=True,
        )
        # Poll hard so the preemptor lands as close to the job start as the
        # test can manage.
        assert atf.wait_for_job_state(
            job_id1, "RUNNING", poll_interval=0.1
        ), f"Attempt {attempt}: low-priority job ({job_id1}) did not start"

        job_id2 = atf.submit_job_sbatch(
            '-c2 -o /dev/null -p highprio --wrap "sleep 20"',
            fatal=True,
        )

        # Bounded by the 10 second PreemptExemptTime above, not by the atf
        # default: past that window the job is no longer exempt and the
        # preemptor is meant to start, so a longer wait would assert the
        # opposite of this test.
        assert not atf.wait_for_job_state(job_id2, "RUNNING", timeout=5, xfail=True), (
            f"Attempt {attempt}: high-priority job ({job_id2}) started while "
            f"({job_id1}) was still within its PreemptExemptTime"
        )
        assert atf.get_job_parameter(job_id2, "JobState") == "PENDING", (
            f"Attempt {attempt}: high-priority job ({job_id2}) should be "
            f"pending, not in a terminal state, during the exempt window"
        )
        assert atf.get_job_parameter(job_id1, "JobState") == "RUNNING", (
            f"Attempt {attempt}: low-priority job ({job_id1}) should still be "
            f"running during its exempt window"
        )

        atf.cancel_jobs([job_id1, job_id2], fatal=True)
