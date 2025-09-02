############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that sacct keeps the requested reservation in ReqReservation."""

import pytest

import atf


# SETUP
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (25, 5),
        "bin/sacct",
        reason="Issue 50285: sacct ReqReservation format field added in 25.05",
    )
    atf.require_accounting(modify=True)
    # A job released from an ended reservation waits for a scheduling cycle.
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def resv_node():
    return next(iter(atf.get_nodes(fatal=True)))


@pytest.fixture(scope="function", autouse=True)
def cleanup_resv():
    yield
    delete_old_resv()


# HELPER FUNCTIONS
def create_resv(node, end, flags=None):
    """Create a reservation to use for testing"""
    atf.run_command(
        f"scontrol create reservation reservationname=test_resv "
        f"{f'flags={flags} ' if flags else ''}"
        f"user={atf.properties['test-user']} start=now end=now+{end} "
        f"nodes={node}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def delete_old_resv():
    """Delete the reservation if it's still there"""
    for _ in atf.timer(fatal=True):
        if "test_resv" not in atf.get_reservations(fatal=True):
            break
        atf.run_command(
            "scontrol delete reservation test_resv",
            user=atf.properties["slurm-user"],
        )


def get_resv_fields(job_id, state):
    """Return the Reservation and ReqReservation sacct fields of the job once
    sacct shows the job in the given state"""
    for _ in atf.timer(fatal=True):
        fields = atf.run_command_output(
            f"sacct -j {job_id} -X -n -P --format=State,Reservation,ReqReservation",
            fatal=True,
        ).strip()
        if fields.startswith(f"{state}|"):
            break
    return fields.split("|")[1:]


# TESTS
def test_reqreservation_after_resv_deleted(resv_node):
    """Checks that ReqReservation outlives the reservation the job ran in"""
    create_resv(resv_node, 60)
    job_id = atf.submit_job_sbatch(
        "--reservation=test_resv -t1 --wrap='true'", fatal=True
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    delete_old_resv()

    _, req_resv = get_resv_fields(job_id, "COMPLETED")
    assert req_resv == "test_resv", "test_resv was not the ReqReservation"


def test_reqreservation_ran_outside_resv(resv_node):
    """Checks ReqReservation of a job that ran after its reservation ended"""
    create_resv(resv_node, 2, "NO_HOLD_JOBS_AFTER_END")
    job_id = atf.submit_job_sbatch(
        "--reservation=test_resv --begin=now+4 -t1 --wrap='true'", fatal=True
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    resv, req_resv = get_resv_fields(job_id, "COMPLETED")
    assert resv == "", "The job should not have run in test_resv"
    assert req_resv == "test_resv", "test_resv was not the ReqReservation"


def test_reqreservation_held_after_resv_end(resv_node):
    """Checks ReqReservation of a job held because its reservation ended"""
    create_resv(resv_node, 2)
    job_id = atf.submit_job_sbatch(
        "--reservation=test_resv --begin=now+4 -t1 --wrap='true'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RESV_DEL_HOLD", fatal=True)

    _, req_resv = get_resv_fields(job_id, "PENDING")
    assert req_resv == "test_resv", "test_resv was not the ReqReservation"
