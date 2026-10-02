############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for SchedulerParameters=time_min_as_soft_limit."""

from datetime import datetime, timedelta

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    if atf.get_config_parameter("SelectType", live=False) == "select/linear":
        atf.require_version(
            (26, 11),
            "sbin/slurmctld",
            reason="Ticket 23101 (26.11+) added this planning improvement for select/linear",
        )
    else:
        atf.require_version(
            (25, 5, 4),
            "sbin/slurmctld",
            reason="Ticket 23426 (25.05.4+) standardized consideration of soft limit for job end times",
        )

    atf.require_config_parameter("SchedulerType", "sched/backfill")
    # The planned start time is only checked if backfill plans a pending job,
    # which it does only at or above bf_min_prio_reserve; the perf config
    # raises it above our job's priority. Use 1 rather than the default 0: the
    # backfill parser logs an explicit 0 as invalid, and 1 only excludes held
    # (priority 0) jobs, which we have none.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_min_prio_reserve", 1)
    )
    # An OverTimeLimit would delay the kill past the time this test waits for
    atf.require_config_parameter("OverTimeLimit", 0)
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter_includes(
        "SchedulerParameters", "time_min_as_soft_limit"
    )
    atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 512)])
    atf.require_config_parameter(
        "PartitionName",
        {
            "lowprio": {"Nodes": "ALL", "Default": "YES"},
        },
    )

    atf.require_slurm_running()


def _parse_time(time_str: str) -> datetime | None:
    """Parse a string into a datetime object, or into None if invalid

    Args:
        time_str (str): String that may be a valid datetime in ISO format

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


def test_time_min_as_soft_limit():
    """Verify that soft limits lead to proper future planning.

    If soft limits are in play, pending jobs should plan against that limit,
    not the hard limit, as long as the soft limit hasn't yet been reached.
    Running jobs should not be killed until the hard limit is reached.
    """

    soft_limit = timedelta(minutes=1)
    hard_limit = timedelta(minutes=2)
    # Jobs are checked every 30s for time limit violations, so this is the time
    # by which we expect a job to be killed for exceeding the hard limit, plus
    # a margin for scheduler latency on slow systems
    kill_time = hard_limit + timedelta(seconds=40)

    job_id1 = atf.submit_job_sbatch(
        f'-c2 -o /dev/null --time-min={soft_limit} --time={hard_limit} --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"First job ({job_id1}) did not start"

    job_id2 = atf.submit_job_sbatch(
        f'-c2 -o /dev/null --time-min={soft_limit} --time={hard_limit} --wrap "sleep infinity"',
        fatal=True,
    )
    assert not atf.wait_for_job_state(
        job_id2, "RUNNING", timeout=5, xfail=True
    ), f"Second job ({job_id2}) started earlier than expected"

    for _ in atf.timer():
        start_str1 = atf.get_job_parameter(job_id1, "StartTime", fatal=True)
        start_dt1 = _parse_time(start_str1)
        if start_dt1 is not None:
            break
    else:
        pytest.fail(f"First job ({job_id1}) has no StartTime")

    for _ in atf.timer(poll_interval=1):
        start_str2 = atf.get_job_parameter(job_id2, "StartTime", fatal=True)
        start_dt2 = _parse_time(start_str2)
        if start_dt2 is not None:
            break
    else:
        pytest.fail(f"Second job ({job_id2}) has no StartTime (received {start_str2})")
    assert _compare_times(
        start_dt1,
        start_dt2,
        expected_delta=soft_limit,
        margin=timedelta(seconds=1),
    ), (
        f"Second job ({job_id2}) start time ({start_dt2}) inconsistent with "
        f"first job ({job_id1}) soft end time ({start_dt1} + {soft_limit})"
    )

    # Test job should be cancelled after hard limit plus periodic kill interval
    assert atf.wait_for_job_state(
        job_id1, "TIMEOUT", timeout=kill_time.total_seconds()
    ), f"Test job ({job_id1}) was not cancelled by reaching hard limit"

    for _ in atf.timer():
        end_str1 = atf.get_job_parameter(job_id1, "EndTime", fatal=True)
        end_dt1 = _parse_time(end_str1)
        if end_dt1 is not None:
            break
    else:
        pytest.fail(f"First job ({job_id1}) has no EndTime")
    # Use direct comparison here to ensure it ran for the full hard limit
    assert end_dt1 - start_dt1 >= hard_limit, (
        f"Test job ({job_id1}) ended before reaching hard limit "
        f"(started {start_str1}, ended {end_str1}, hard limit {hard_limit})"
    )
