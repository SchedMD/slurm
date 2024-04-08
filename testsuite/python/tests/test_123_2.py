############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=False)
def delete_old_resv():
    """Delete the reservation if it's still there"""
    atf.run_command(
        f"scontrol delete reservation resv1",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


@pytest.fixture(scope="function", autouse=False)
def create_resv(delete_old_resv):
    """Create a reservation to use for testing"""
    atf.run_command(
        f"scontrol create reservation reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now end=now+1 "
        f"nodes=node0",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_clear_job_expired_deadline(create_resv):
    """Put several jobs in the queue that will go past the resv deadline"""
    job_id = atf.submit_job_sbatch(
        sbatch_args=rf" -w node0 --deadline=now+4 "
        f"--begin=now --reservation=resv1 "
        f'--wrap="sleep 300"'
    )

    assert job_id != 0, "Couldn't start job batch"

    """Wait for the JobState to change to RESV_DEL_HOLD"""
    atf.repeat_until(
        lambda: atf.run_command_output(f"scontrol show job {job_id} | grep JobState"),
        lambda resp: "RESV_DEL_HOLD" in resp,
        timeout=60,
        poll_interval=1,
    )

    """Wait for the JobState to change to DEADLINE"""
    atf.repeat_until(
        lambda: atf.run_command_output(f"scontrol show job {job_id} | grep JobState"),
        lambda resp: "DEADLINE" in resp,
        timeout=60,
        poll_interval=1,
    )

    """Assert that the JobState is DEADLINE"""
    output = atf.run_command_output(f"scontrol show job {job_id} | grep JobState")
    assert (
        "DEADLINE" in output
    ), "The JobState did not change to DEADLINE when the reservation expired."

    """Verify that the queue is empty"""
    output = atf.run_command_output(f"squeue")
    assert (
        str(job_id) not in output
    ), f"Job {job_id} was still in the queue and should have been deleted."
