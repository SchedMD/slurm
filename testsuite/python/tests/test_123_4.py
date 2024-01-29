############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()
    yield
    cleanup()


def cleanup():
    result = atf.run_command(
        f"scontrol delete reservationname={res_name}", user=atf.properties["slurm-user"]
    )
    assert result["exit_code"] == 0, "Couldn't delete the reservation!"


def test_reservation_user():
    """Test that a reservation created for SlurmUser can't be used by atf"""

    # Create the reservation for user slurm
    global res_name
    res_name = "resv1"
    create_res = (
        f"scontrol create reservationname={res_name} "
        f"user={atf.properties['slurm-user']} start=now duration=1 nodecnt=1"
    )
    result = atf.run_command(create_res, user=atf.properties["slurm-user"])
    assert result["exit_code"] == 0, "Couldn't create the reservation!"

    # Try to run a job as atf
    result = atf.run_command(
        f"srun -N1 --reservation={res_name} true", user=atf.properties["test-user"]
    )
    assert result["exit_code"] != 0, "The job should have been denied for user!"
    assert (
        "Access denied" in result["stderr"]
    ), "The job should have been denied for user!"
