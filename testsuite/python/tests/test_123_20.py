############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Ticket 26111.

Verify that relative Duration+= and Duration-= updates apply to the effective
duration of a reservation whose end was last set with EndTime, either when the
reservation was created or by a later update. Such a reservation has no stored
duration, so the delta must move EndTime relative to its current value.

Start and end times are minute-aligned so the expected result does not depend
on how sub-minute remainders are handled.
"""

import os
from datetime import datetime, timedelta

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
resv_name = f"{test_name}_resv"

TIME_FORMAT = "%Y-%m-%dT%H:%M:%S"
DELTA_MINUTES = 60


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11, 0),
        "sbin/slurmctld",
        reason="Ticket 26111: Duration+=/-= after EndTime fixed in 26.11.0",
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def reservation(request):
    """Create a reservation whose end was last set with EndTime."""
    start = (datetime.now() + timedelta(hours=1)).replace(second=0, microsecond=0)
    end = start + timedelta(days=1)

    if request.param == "create":
        length = f"EndTime={end.strftime(TIME_FORMAT)}"
    else:
        length = "Duration=12:00:00"

    atf.run_command(
        f"scontrol create reservation ReservationName={resv_name} "
        f"StartTime={start.strftime(TIME_FORMAT)} {length} NodeCnt=1 "
        f"Users={atf.properties['test-user']}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    if request.param == "update":
        atf.run_command(
            f"scontrol update ReservationName={resv_name} "
            f"EndTime={end.strftime(TIME_FORMAT)}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    yield start, end

    atf.run_command(
        f"scontrol delete ReservationName={resv_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def duration_str(delta):
    """Format a timedelta the way scontrol prints a reservation Duration."""
    hours, remainder = divmod(delta.seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    hms = f"{hours:02d}:{minutes:02d}:{seconds:02d}"
    return f"{delta.days}-{hms}" if delta.days else hms


@pytest.mark.parametrize(
    "reservation",
    [
        pytest.param("create", id="EndTime_at_create"),
        pytest.param("update", id="EndTime_by_update"),
    ],
    indirect=True,
)
@pytest.mark.parametrize("sign", ["+", "-"])
def test_relative_duration_after_endtime(reservation, sign):
    """Duration+= and Duration-= must move the EndTime that was set."""
    start, end = reservation
    # Slurm moves EndTime by elapsed seconds and prints Duration as elapsed
    # seconds, so compute both in epoch time in case the window crosses a
    # daylight saving change.
    delta = DELTA_MINUTES * 60 if sign == "+" else -DELTA_MINUTES * 60
    expected_end = datetime.fromtimestamp(end.timestamp() + delta)
    expected_end_str = expected_end.strftime(TIME_FORMAT)
    expected_duration = duration_str(
        timedelta(seconds=expected_end.timestamp() - start.timestamp())
    )

    before = atf.get_reservations(fatal=True)[resv_name]
    assert before["StartTime"] == start.strftime(
        TIME_FORMAT
    ), f"StartTime should be {start.strftime(TIME_FORMAT)}, got {before['StartTime']}"
    assert before["EndTime"] == end.strftime(
        TIME_FORMAT
    ), f"EndTime should be {end.strftime(TIME_FORMAT)}, got {before['EndTime']}"

    operation = f"Duration{sign}={DELTA_MINUTES}"
    assert (
        atf.run_command_exit(
            f"scontrol update ReservationName={resv_name} {operation}",
            user=atf.properties["slurm-user"],
        )
        == 0
    ), f"scontrol update should accept {operation}"

    after = atf.get_reservations(fatal=True)[resv_name]
    assert after["EndTime"] == expected_end_str, (
        f"{operation} should move EndTime from {before['EndTime']} to "
        f"{expected_end_str}, got {after['EndTime']}"
    )
    assert after["Duration"] == expected_duration, (
        f"{operation} should produce Duration={expected_duration}, "
        f"got {after['Duration']}"
    )
