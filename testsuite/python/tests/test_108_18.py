############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol requeue and requeuehold of external jobs."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 166: slurm-bridge external job requeue added in 26.11",
    )
    atf.require_config_parameter("JobRequeue", "1")
    atf.require_slurm_running()


@pytest.mark.parametrize("finished", [False, True], ids=["running", "finished"])
@pytest.mark.parametrize("command", ["requeue", "requeuehold"])
def test_requeue_external_job(command, finished):
    """Test that scontrol returns a running or finished external job to pending."""

    job_id = atf.submit_job_sbatch("--external --requeue -N1 -t1", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    if finished:
        atf.cancel_jobs([job_id], fatal=True)

    result = atf.run_command(
        f"scontrol {command} {job_id}", user=atf.properties["slurm-user"]
    )
    assert (
        result["exit_code"] == 0
    ), f"scontrol {command} {job_id} failed: {result['stderr']}"
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), f"External job {job_id} did not return to pending"

    job = atf.get_job(job_id)
    assert job["Restarts"] == 1, job
    assert job["BatchFlag"] == 0, job

    if command == "requeuehold":
        assert job["Priority"] == 0, job
        atf.run_command(
            f"scontrol release {job_id}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        assert (
            atf.get_job_parameter(job_id, "Priority") > 0
        ), f"External job {job_id} is still held after release"


@pytest.mark.parametrize(
    "submit,error",
    [
        (
            lambda: atf.submit_job_sbatch(
                "--external --no-requeue -N1 -t1", fatal=True
            ),
            "Requested operation is presently disabled",
        ),
        (
            lambda: atf.submit_job_salloc("--no-shell -N1 -t1", fatal=True),
            "Only batch jobs are accepted or processed",
        ),
    ],
    ids=["no_requeue", "salloc"],
)
def test_requeue_refused(submit, error):
    """Test that scontrol requeue still refuses jobs that can't be requeued."""

    job_id = submit()
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    result = atf.run_command(
        f"scontrol requeue {job_id}", user=atf.properties["slurm-user"], xfail=True
    )
    assert result["exit_code"] != 0, f"scontrol requeue {job_id} did not fail"
    assert error in result["stderr"], result["stderr"]
    assert (
        atf.get_job_parameter(job_id, "JobState") == "RUNNING"
    ), f"Job {job_id} did not stay running"
