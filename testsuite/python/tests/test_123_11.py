############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify FLEX,MAGNETIC reservations attract and run eligible jobs."""

import os

import pytest

import atf

pytestmark = pytest.mark.slow

test_name = os.path.splitext(os.path.basename(__file__))[0]
res_name = f"res_{test_name}"

# The regression only triggers when the job's time limit extends past the
# reservation's remaining window, so the requested limit must exceed it.
RESV_DURATION_MINUTES = 5
JOB_TIME_LIMIT_MINUTES = 30


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmctld",
        reason="Ticket 25213: FLEX end-time regression fixed only in 26.05+",
    )
    atf.require_nodes(2)
    atf.require_slurm_running()


def create_resv(flags, user=None):
    partition = atf.default_partition()
    if user is None:
        user = atf.properties["test-user"]
    slurm_user = atf.properties["slurm-user"]

    atf.run_command(
        f"scontrol create reservationname={res_name} "
        f"start=now duration={RESV_DURATION_MINUTES} nodecnt=1 "
        f"partition={partition} user={user} "
        f"flags={flags}",
        user=slurm_user,
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        if atf.get_reservation_parameter(res_name, "State") == "ACTIVE":
            break


@pytest.fixture()
def delete_resv():
    yield

    slurm_user = atf.properties["slurm-user"]
    atf.cancel_all_jobs()
    atf.run_command(
        f"scontrol delete reservationname={res_name}",
        user=slurm_user,
    )


@pytest.mark.parametrize("flags", ["FLEX,MAGNETIC", "FLEX,MAGNETIC,DAILY"])
def test_flex_magnetic_job_runs(flags, delete_resv):
    """A FLEX,MAGNETIC reservation attracts and runs an eligible job whose
    time limit exceeds the reservation's remaining window."""

    create_resv(flags)

    job_id = atf.submit_job_sbatch(
        f'-N1 -t{JOB_TIME_LIMIT_MINUTES} -o /dev/null -e /dev/null --wrap "sleep 5"',
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "Reservation") == res_name
    ), "Job should be associated with the magnetic reservation"


def test_flex_magnetic_ineligible_job_not_attracted(delete_resv):
    """A MAGNETIC reservation must not attract a job from a user that does
    not meet its access control requirements."""

    slurm_user = atf.properties["slurm-user"]

    # Reservation permits only slurm_user; the job runs as the default
    # test-user, which is not permitted and so must not be attracted.
    create_resv("FLEX,MAGNETIC", slurm_user)

    job_id = atf.submit_job_sbatch(
        f'-N1 -t{JOB_TIME_LIMIT_MINUTES} -o /dev/null -e /dev/null --wrap "sleep 5"',
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "Reservation") != res_name
    ), "Job should not be attracted by a reservation it cannot access"
