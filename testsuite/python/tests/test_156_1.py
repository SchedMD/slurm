############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2, [("ThreadsPerCore", 2), ("Cores", 4), ("Sockets", 1)])
    atf.require_slurm_running()


@pytest.mark.xfail(
    atf.get_version() < (26, 5)
    and atf.get_config_parameter("SelectType", live=False) != "select/linear",
    reason="Ticket 20799: Concurrent steps are not started and identified correctly",
)
def test_no_missing_step():
    """
    This test confirms steps 0 and 1 are run concurrently, and that step 2 is
    queued while they hold the resources and then runs as soon as they free up,
    keeping the same step id it was assigned at submission.
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

    # Issue 50938: as of 26.11 a queued step is assigned its real StepId at
    # submission, so the third step is visible as <jobid>.2 in PENDING while it
    # waits. Before 26.11 it had no id until it launched.
    id_at_submit = atf.get_version("bin/srun") >= (26, 11)

    def _state(steps, step_id):
        return steps.get(f"{job_id}.{step_id}", {}).get("State")

    # The first two steps run in parallel while the third is queued.
    steps = {}
    for _ in atf.timer():
        steps = atf.get_steps(job_id)
        if (
            _state(steps, 0) == "RUNNING"
            and _state(steps, 1) == "RUNNING"
            and (
                _state(steps, 2) == "PENDING"
                if id_at_submit
                else f"{job_id}.2" not in steps
            )
        ):
            break
    else:
        assert False, (
            f"First two steps should run in parallel before the third one, "
            f"got {steps}"
        )

    # The third step runs as soon as the second ends, so in parallel with the
    # first one.
    for _ in atf.timer():
        steps = atf.get_steps(job_id)
        if (
            _state(steps, 0) == "RUNNING"
            and f"{job_id}.1" not in steps
            and _state(steps, 2) == "RUNNING"
        ):
            break
    else:
        assert (
            False
        ), f"The third step should run as soon as the second ends, got {steps}"
