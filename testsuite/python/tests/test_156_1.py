############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2, [("ThreadsPerCore", 2), ("Cores", 4), ("Sockets", 1)])
    atf.require_slurm_running()


def test_no_missing_step(tmp_path):
    """
    This test confirms steps 0 and 1 are run concurrently, and that step 2 is
    run as soon as there are available resources, and with the correct step id.
    See Ticket 20799 for more details
    """
    atf.make_bash_script(
        "my_script.sh",
        """
srun -l -n 3 --hint=nomultithread --distribution=pack --exact sleep 20 &
sleep 3
srun -l -n 3 --hint=nomultithread --distribution=pack --exact sleep 10 &
sleep 3
srun -l -n 2 --hint=nomultithread --distribution=pack --exact sleep 10 &

wait $(jobs -p)
    """,
    )
    job_id = atf.submit_job_sbatch("-N2 -n16 my_script.sh", fatal=True)

    assert atf.repeat_until(
        lambda: atf.get_steps(job_id),
        lambda steps: f"{job_id}.0" in steps
        and f"{job_id}.1" in steps
        and f"{job_id}.2" not in steps,
    ), "First two steps should run in parallel before the third one"

    assert atf.repeat_until(
        lambda: atf.get_steps(job_id),
        lambda steps: f"{job_id}.0" in steps
        and f"{job_id}.1" not in steps
        and f"{job_id}.2" in steps,
    ), "The third step should run as soon as the second ends, so in parallel with the first one"
