############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import pytest
import atf


def setup_module():
    atf.require_nodes(4, [("CPUs", 2)])
    atf.require_slurm_running()


def submit_running_job():
    """Submit a job and wait for it to be running. Returns job_id."""
    job_id = atf.submit_job_sbatch("-n1 --wrap 'srun sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)
    return job_id


def submit_multistep_job():
    """Submit a batch job that launches 2 steps in parallel which sleep
    indefinitely. Returns jobid.
    """
    script = atf.module_tmp_path / "multistep.sh"
    atf.make_bash_script(
        script,
        """srun -n1 --mem=0 --overlap sleep infinity &
        srun -n1 --mem=0 --overlap sleep infinity &
        wait""",
    )
    job_id = atf.submit_job_sbatch(f"-N2 -O {script}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)
    return job_id


def get_running_user_steps(job_id):
    """Return set of running user step keys like {'<job_id>.0', '<job_id>.1'}."""
    steps = atf.get_steps(job_id, quiet=True)
    return {
        k
        for k, v in steps.items()
        if v.get("State", "") == "RUNNING" and ".batch" not in k and ".extern" not in k
    }


@pytest.mark.parametrize(
    "id_type,step",
    [
        ("JobId", False),
        ("JobId", True),
        ("SLUID", False),
        ("SLUID", True),
    ],
)
def test_verbose_signal(id_type, step):
    """Verify scancel --verbose shows correct 'Signal N to job/step <id>' message."""

    job_id = submit_running_job()
    identifier = str(atf.get_job_parameter(job_id, id_type))

    target = f"{identifier}.0" if step else identifier
    entity = "step" if step else "job"

    result = atf.run_command(f"scancel --signal SIGHUP {target} --verbose")
    assert re.search(
        rf"Signal 1 to {entity} {re.escape(target)}", result["stderr"]
    ), f"Expected 'Signal 1 to {entity} {target}' in stderr: {result['stderr']}"

    atf.wait_for_job_state(job_id, "FAILED", fatal=True)


def test_verbose_mixed_ids():
    """Verify scancel --verbose output with a mix of job IDs, step IDs, SLUIDs, and SLUID steps.

    Equivalent to:
      scancel --signal SIGHUP <job1>,<job2>.0,<sluid3>,<sluid4>.0 --verbose
    Expected output lines (in any order):
      scancel: Signal 1 to job <job1>
      scancel: Signal 1 to step <job2>.0
      scancel: Signal 1 to job <sluid3>
      scancel: Signal 1 to step <sluid4>.0
    """

    job1 = submit_running_job()
    job2 = submit_running_job()
    job3 = submit_running_job()
    job4 = submit_running_job()

    sluid3 = atf.get_job_parameter(job3, "SLUID")
    sluid4 = atf.get_job_parameter(job4, "SLUID")

    ids = f"{job1},{job2}.0,{sluid3},{sluid4}.0"
    result = atf.run_command(f"scancel --signal SIGHUP {ids} --verbose")
    stderr = result["stderr"]

    assert re.search(
        rf"Signal 1 to job {job1}", stderr
    ), f"Missing 'Signal 1 to job {job1}' in: {stderr}"
    assert re.search(
        rf"Signal 1 to step {job2}\.0", stderr
    ), f"Missing 'Signal 1 to step {job2}.0' in: {stderr}"
    assert re.search(
        rf"Signal 1 to job {re.escape(sluid3)}", stderr
    ), f"Missing 'Signal 1 to job {sluid3}' in: {stderr}"
    assert re.search(
        rf"Signal 1 to step {re.escape(sluid4)}\.0", stderr
    ), f"Missing 'Signal 1 to step {sluid4}.0' in: {stderr}"

    # Verify all four jobs are actually terminated
    for jid in [job1, job2, job3, job4]:
        atf.wait_for_job_state(jid, "FAILED", fatal=True)


@pytest.mark.parametrize(
    "id_type,step,expected_state",
    [
        ("JobId", False, "CANCELLED"),
        ("JobId", True, "FAILED"),
        ("SLUID", False, "CANCELLED"),
        ("SLUID", True, "FAILED"),
    ],
)
def test_cancel_terminates(id_type, step, expected_state):
    """Verify scancel with job/step ID or SLUID actually terminates the job."""
    job_id = submit_running_job()
    identifier = str(atf.get_job_parameter(job_id, id_type))
    target = f"{identifier}.0" if step else identifier

    atf.run_command(f"scancel {target}", fatal=True)
    atf.wait_for_job_state(job_id, expected_state, fatal=True)


@pytest.mark.parametrize("id_type", ["JobId", "SLUID"])
def test_signal_delivers(id_type):
    """Verify scancel --signal with a job ID or SLUID delivers SIGSTOP/SIGCONT."""

    job_id = submit_running_job()
    identifier = str(atf.get_job_parameter(job_id, id_type))

    atf.run_command(f"scancel --signal SIGSTOP {identifier}", fatal=True)
    atf.wait_for_job_state(job_id, "STOPPED", fatal=True)

    atf.run_command(f"scancel --signal SIGCONT {identifier}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    atf.run_command(f"scancel {identifier}", fatal=True)
    atf.wait_for_job_state(job_id, "CANCELLED", fatal=True)


def test_cancel_mixed_terminates():
    """Verify scancel with mixed job IDs, step IDs, SLUIDs, and SLUID steps terminates all jobs."""

    job1 = submit_running_job()
    job2 = submit_running_job()
    job3 = submit_running_job()
    job4 = submit_running_job()

    sluid3 = atf.get_job_parameter(job3, "SLUID")
    sluid4 = atf.get_job_parameter(job4, "SLUID")

    ids = f"{job1},{job2}.0,{sluid3},{sluid4}.0"
    atf.run_command(f"scancel {ids}", fatal=True)

    # job cancel -> CANCELLED, step cancel -> FAILED
    atf.wait_for_job_state(job1, "CANCELLED", fatal=True)
    atf.wait_for_job_state(job2, "FAILED", fatal=True)
    atf.wait_for_job_state(job3, "CANCELLED", fatal=True)
    atf.wait_for_job_state(job4, "FAILED", fatal=True)


@pytest.mark.parametrize("id_type", ["JobId", "SLUID"])
def test_cancel_one_step(id_type):
    """Cancel only step 0 in a multi-step job; step 1 must survive."""

    job_id = submit_multistep_job()
    identifier = str(atf.get_job_parameter(job_id, id_type))

    result = atf.run_command(f"scancel {identifier}.0 --verbose")
    assert re.search(
        rf"Terminating step {re.escape(identifier)}\.0", result["stderr"]
    ), f"Expected 'Terminating step {identifier}.0' in stderr: {result['stderr']}"

    # Step 1 should still be running
    assert atf.repeat_until(
        lambda: get_running_user_steps(job_id),
        lambda steps: f"{job_id}.0" not in steps and f"{job_id}.1" in steps,
        poll_interval=1,
    ), "Step 1 should still be running after cancelling step 0"
