############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify srun --jobid can attach a step to a running job array task.

Covers ticket 25328: attaching with --overlap to a running array task
both when all tasks are running and when sibling tasks are still pending.
"""

import pytest

import atf

array_size = 4


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "bin/srun",
        reason="Ticket 25328: srun --jobid cannot attach to an array task before this fix",
    )

    atf.require_config_parameter("MaxArraySize", array_size, ">=")
    atf.require_nodes(array_size)
    atf.require_slurm_running()


def test_srun_jobid_attach_to_array_task():
    """Test that srun --jobid=<array_job_id>_<array_task_id> can attach to a
    running task of a job array"""

    # Submit a job array where each task just sleeps, and wait for it to run
    job_id = atf.submit_job_sbatch(
        f"-N1 --array=0-{array_size - 1} -t5 --wrap='sleep infinity'",
        fatal=True,
    )
    for task_id in range(array_size):
        # A task only gets its own record once it is scheduled
        raw_job_id = 0
        for t in atf.timer(fatal=True):
            raw_job_id = atf.get_job_id_from_array_task(job_id, task_id)
            if raw_job_id != 0:
                break
        atf.wait_for_job_state(raw_job_id, "RUNNING", fatal=True)

        # Both the array (job_id_task_id) and the raw task job id are valid
        for jobid in (f"{job_id}_{task_id}", raw_job_id):
            results = atf.run_job(f"--jobid={jobid} --overlap printenv SLURM_JOB_ID")
            assert (
                results["exit_code"] == 0
            ), f"srun --jobid={jobid} failed to attach to the running array task"
            assert results["stdout"].strip() == str(raw_job_id), (
                f"srun --jobid={jobid} attached to the wrong job "
                f"(expected {raw_job_id}, got '{results['stdout'].strip()}')"
            )
            assert (
                "error" not in results["stderr"].lower()
            ), f"srun --jobid={jobid} emitted errors: {results['stderr']}"


def test_srun_jobid_attach_with_pending_sibling():
    """Test that srun --jobid=<array_job_id>_<array_task_id> can attach to a
    running array task while sibling tasks are still pending"""

    # %1 keeps only one task running at a time, so the rest stay pending
    job_id = atf.submit_job_sbatch(
        f"-N1 --array=0-{array_size - 1}%1 -t5 --wrap='sleep infinity'",
        fatal=True,
    )

    # The lowest-index task runs first; wait for its record to appear
    running_task = 0
    raw_job_id = 0
    for t in atf.timer(fatal=True):
        raw_job_id = atf.get_job_id_from_array_task(job_id, running_task)
        if raw_job_id != 0:
            break
    atf.wait_for_job_state(raw_job_id, "RUNNING", fatal=True)
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    # Both the array (job_id_task_id) and raw task job id must resolve to the
    # running task even while its siblings are still pending
    for jobid in (f"{job_id}_{running_task}", raw_job_id):
        results = atf.run_job(f"--jobid={jobid} --overlap printenv SLURM_JOB_ID")
        assert (
            results["exit_code"] == 0
        ), f"srun --jobid={jobid} failed to attach with pending siblings"
        assert results["stdout"].strip() == str(raw_job_id), (
            f"srun --jobid={jobid} attached to the wrong job "
            f"(expected {raw_job_id}, got '{results['stdout'].strip()}')"
        )
        assert (
            "error" not in results["stderr"].lower()
        ), f"srun --jobid={jobid} emitted errors: {results['stderr']}"


def test_srun_jobid_attach_to_pending_task_fails():
    """Test that srun --jobid=<array_job_id>_<array_task_id> does not attach to
    a pending array task nor silently resolve to a running sibling"""

    # %1 keeps only the lowest-index task running; higher indexes stay pending
    job_id = atf.submit_job_sbatch(
        f"-N1 --array=0-{array_size - 1}%1 -t5 --wrap='sleep infinity'",
        fatal=True,
    )

    running_task = 0
    running_job_id = 0
    for t in atf.timer(fatal=True):
        running_job_id = atf.get_job_id_from_array_task(job_id, running_task)
        if running_job_id != 0:
            break
    atf.wait_for_job_state(running_job_id, "RUNNING", fatal=True)
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    pending_task = array_size - 1
    results = atf.run_job(
        f"--jobid={job_id}_{pending_task} --overlap printenv SLURM_JOB_ID"
    )
    assert (
        results["exit_code"] != 0
    ), f"srun --jobid={job_id}_{pending_task} wrongly attached to a pending task"
    assert results["stdout"].strip() != str(running_job_id), (
        f"srun --jobid={job_id}_{pending_task} wrongly resolved to the running "
        f"task {running_job_id}"
    )
