############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Ticket 25600.

Verify that a reservation created with Duration=INFINITE keeps its effective
one-year duration when only StartTime is updated, and that relative
Duration+= and Duration-= updates apply to that effective duration.
"""

import os

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
resv_name = f"{test_name}_resv"

EXPECTED_DURATION = "365-00:00:00"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1)
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def reservation():
    atf.run_command(
        f"scontrol create reservation ReservationName={resv_name} "
        f"StartTime=NOW+1hour Duration=INFINITE NodeCnt=1 "
        f"Users={atf.properties['test-user']}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield resv_name

    atf.run_command(
        f"scontrol delete ReservationName={resv_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.mark.xfail(
    atf.get_version() < (26, 11),
    reason="Ticket 25600: StartTime-only update overflows end_time for Duration=INFINITE reservations",
)
def test_update_start_time_preserves_infinite_duration(reservation):
    """Updating StartTime alone must not expand an INFINITE reservation."""

    before = atf.get_reservations(fatal=True)[reservation]
    assert before["Duration"] == EXPECTED_DURATION, (
        "Duration=INFINITE should create a one-year reservation; "
        f"got {before['Duration']}"
    )

    atf.run_command(
        f"scontrol update ReservationName={reservation} StartTime=NOW+2hours",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    after = atf.get_reservations(fatal=True)[reservation]

    assert after["StartTime"] > before["StartTime"], "StartTime should move forward"
    assert after["Duration"] == EXPECTED_DURATION, (
        "A StartTime-only update should preserve the one-year effective "
        f"duration of an INFINITE reservation; got {after['Duration']}"
    )


@pytest.mark.parametrize(
    ("operation", "expected_duration"),
    [
        ("Duration+=60", "365-01:00:00"),
        ("Duration-=60", "364-23:00:00"),
    ],
)
@pytest.mark.xfail(
    atf.get_version() < (26, 11),
    reason="Ticket 25600: relative Duration update treats INFINITE as a minute count",
)
def test_update_infinite_duration(reservation, operation, expected_duration):
    """Relative updates must use the effective one-year duration."""

    atf.run_command(
        f"scontrol update ReservationName={reservation} {operation}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    after = atf.get_reservations(fatal=True)[reservation]

    assert after["Duration"] == expected_duration, (
        f"{operation} should produce Duration={expected_duration}; "
        f"got {after['Duration']}"
    )
