############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a cancelled stepmgr job refuses new steps."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="MR 4653: stepmgr refuses steps after the job ends since 26.11",
    )
    # --stepmgr is silently dropped without it, see --stepmgr in sbatch(1)
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def submit_stepmgr_job(script):
    """Submit a --stepmgr job and wait until its script signals it is ready.

    slurmctld also refuses steps of an ending job, with a different error,
    so make sure the stepmgr is really the one in use.
    """
    job_id = atf.submit_job_sbatch(f"--stepmgr -t2 {script}", fatal=True)
    atf.wait_for_file("ready", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "StepMgrEnabled", fatal=True) == "Yes"
    ), f"job {job_id} is not managed by a stepmgr"
    return job_id


def test_step_after_cancel():
    """A step started from a SIGTERM trap after scancel is refused."""
    atf.make_bash_script(
        "cancel.sh",
        """#SBATCH -o cancel.out
trap 'srun --overlap true; echo "srun rc=$?"' TERM
touch ready
sleep infinity &
wait
""",
    )
    job_id = submit_stepmgr_job("cancel.sh")

    atf.run_command(f"scancel {job_id}", fatal=True)

    atf.assert_file_contents(
        "cancel.out", "Job/step already completing or completed", contains=True
    )
    atf.assert_file_contents("cancel.out", "srun rc=1", contains=True)


def test_pending_step_after_cancel():
    """A step still pending when the job is cancelled is refused when woken."""
    atf.make_bash_script(
        "pending.sh",
        """#SBATCH -n1 -o pending.out
srun -n1 sleep infinity &
touch ready
wait
""",
    )
    job_id = submit_stepmgr_job("pending.sh")
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.run_command(
        f"srun --jobid={job_id} -n1 true >pending_srun.out 2>&1 &", fatal=True
    )
    assert atf.get_pending_async_step(job_id), "no pending step to wake"

    atf.run_command(f"scancel {job_id}", fatal=True)

    atf.assert_file_contents(
        "pending_srun.out", "Job/step already completing or completed", contains=True
    )


def test_step_after_signal_term():
    """scancel --signal=TERM does not end the job, so steps still start."""
    atf.make_bash_script(
        "signal.sh",
        """#SBATCH -o signal.out
srun --overlap bash -c 'trap "touch got_term" TERM; touch ready; sleep infinity & wait' &
while [ ! -e go ]; do sleep 0.2; done
srun --overlap true; echo "srun rc=$?"
sleep infinity
""",
    )
    job_id = submit_stepmgr_job("signal.sh")

    atf.run_command(f"scancel --signal=TERM {job_id}", fatal=True)
    # The running step trapping SIGTERM shows slurmd delivered the signal
    atf.wait_for_file("got_term", fatal=True)
    atf.run_command("touch go", fatal=True)

    atf.assert_file_contents("signal.out", "srun rc=0", contains=True)
