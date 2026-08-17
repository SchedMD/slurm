############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job state transition jobcomp events with a finish-only plugin."""

import re

import pytest

import atf

jobcomp_log = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global jobcomp_log

    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51114: job state transition jobcomp events added in 26.11",
    )

    # slurmctld writes the log from its own working directory, so it needs
    # an absolute path
    jobcomp_log = atf.module_tmp_path / "jobcomp.log"

    atf.require_config_parameter("JobCompType", "jobcomp/filetxt")
    atf.require_config_parameter("JobCompLoc", str(jobcomp_log))
    for param in (
        "enable_job_queue",
        "enable_job_requeue",
        "enable_job_hold",
        "enable_job_release",
    ):
        atf.require_config_parameter_includes("JobCompParams", param)
    # A requeued job is only eligible again after requeue_delay, which defaults
    # to the AuthInfo cred_expire (120 seconds), and is only started by the
    # next scheduling cycle. The (key, value) form replaces a value the site
    # config already sets.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


def get_job_records(job_id):
    """Return the jobcomp records logged for a job."""
    output = atf.run_command_output(f"cat {jobcomp_log}", quiet=True, fatal=True)
    return re.findall(rf"^JobId={job_id} .*$", output, flags=re.MULTILINE)


def wait_for_job_records(job_id, count):
    """Poll the jobcomp log until job_id has count records, then return them."""
    for t in atf.timer():
        records = get_job_records(job_id)
        if len(records) >= count:
            break
    else:
        assert False, (
            f"Job {job_id} never got {count} records in {jobcomp_log}: "
            f"{get_job_records(job_id)}"
        )
    return records


def test_hold_release_single_completion_record():
    """Held and released jobs log exactly one record in a finish-only plugin.

    The job queue, hold, and release jobcomp events must not add records
    to a plugin that only supports job finish events, like jobcomp/filetxt.
    A job that is submitted held, released, held again while it waits for
    its begin time, released, and runs to completion must produce exactly
    one record: its completion.
    """
    atf.make_bash_script("job1.sh", "true")
    job_id = atf.submit_job_sbatch(
        "--hold --begin=now+1hour -t1 -o /dev/null -J jobcomp1 job1.sh", fatal=True
    )

    assert (
        atf.get_job_parameter(job_id, "Reason", fatal=True) == "JobHeldUser"
    ), f"Job {job_id} submitted with --hold should be pending as JobHeldUser"
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    # The begin time keeps the job pending, so this hold is a transition
    atf.run_command(f"scontrol hold {job_id}", fatal=True)
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.run_command(f"scontrol update JobId={job_id} StartTime=now", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    records = wait_for_job_records(job_id, 1)
    assert len(records) == 1, f"Expected a single record, got: {records}"
    assert (
        "JobState=COMPLETED" in records[0]
    ), f"The single record should be the job completion: {records[0]}"


def test_requeue_two_completion_records():
    """A requeued job logs exactly two records in a finish-only plugin.

    A requeue generates a job finish record saying REQUEUED, and the job
    requeue event itself must not add records to a plugin that only
    supports job finish events. A job that runs, is requeued, runs again,
    and is cancelled must produce exactly two records.
    """
    atf.make_bash_script("job2.sh", "sleep infinity")
    job_id = atf.submit_job_sbatch(
        "--requeue -t2 -o /dev/null -J jobcomp2 job2.sh", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    atf.run_command(f"scontrol requeue {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    records = wait_for_job_records(job_id, 2)
    assert len(records) == 2, f"Expected two records, got: {records}"
    assert (
        "JobState=REQUEUED" in records[0]
    ), f"The first record should be the requeued run: {records[0]}"
    assert (
        "JobState=CANCELLED" in records[1]
    ), f"The second record should be the job cancellation: {records[1]}"
