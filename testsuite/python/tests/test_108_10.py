############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import json
import re
import atf


def setup_module():
    atf.require_version(
        (26, 5),
        "bin/scontrol",
        reason="Ticket 22180: SLUID availability added in 26.05+",
    )
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


def get_and_assert_sluid(job_id):
    """Get the SLUID for a given job_id via scontrol -d show job."""
    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid is not None, f"Job {job_id} has no SLUID"
    return sluid


def submit_running_job():
    """Submit a job and wait for it to be running. Returns job_id."""
    job_id = atf.submit_job_sbatch("-n1 --wrap 'srun sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


def submit_pending_job():
    """Submit a held job so it stays PENDING. Returns job_id."""
    job_id = atf.submit_job_sbatch(
        "-n1 --hold --wrap 'srun sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    return job_id


def submit_requeueable_job():
    """Submit a requeueable job and wait for it to be running. Returns job_id."""
    job_id = atf.submit_job_sbatch(
        "-n1 --requeue --wrap 'srun sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


# scontrol update job
def test_update_admincomment_by_sluid():
    """Update AdminComment via SLUID and verify with scontrol show job <SLUID>."""

    job_id = submit_running_job()
    sluid = get_and_assert_sluid(job_id)

    comment = "test_sluid_comment"
    atf.run_command(
        f"scontrol update job={sluid} admincomment={comment}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    output = atf.run_command_output(f"scontrol -d show job {sluid}", fatal=True)
    assert re.search(
        rf"AdminComment={comment}", output
    ), f"Expected AdminComment={comment} in show job output: {output}"


# scontrol hold / release
def test_hold_release_by_sluid():
    """Hold and release a pending job using its SLUID. Hold sets priority to 0
    and reason to JobHeldAdmin. Release restores priority and clears the hold.
    """

    job_id = submit_pending_job()
    sluid = get_and_assert_sluid(job_id)

    # The job is already held from submission.
    atf.run_command(
        f"scontrol release {sluid}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # After release, priority should be non-zero
    priority = atf.get_job_parameter(job_id, "Priority")
    assert int(priority) > 0, f"Expected Priority > 0 after release, got {priority}"

    # Hold by SLUID
    atf.run_command(
        f"scontrol hold {sluid}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    priority = atf.get_job_parameter(job_id, "Priority")
    reason = atf.get_job_parameter(job_id, "Reason")
    assert int(priority) == 0, f"Expected Priority=0 after hold, got {priority}"
    assert (
        "JobHeldAdmin" in reason
    ), f"Expected Reason=JobHeldAdmin after hold, got {reason}"


def test_hold_release_mixed_ids():
    """Hold and release two pending jobs using a mix of job_id and SLUID."""

    job_id1 = submit_pending_job()
    job_id2 = submit_pending_job()
    sluid2 = get_and_assert_sluid(job_id2)

    # Both are held from submission. Release them first.
    atf.run_command(
        f"scontrol release {job_id1} {sluid2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for jid in [job_id1, job_id2]:
        priority = atf.get_job_parameter(jid, "Priority")
        assert (
            int(priority) > 0
        ), f"Expected Priority > 0 after release for job {jid}, got {priority}"

    # Hold both: one by job_id, one by SLUID
    sluid1 = get_and_assert_sluid(job_id1)
    atf.run_command(
        f"scontrol hold {job_id1} {sluid2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for jid in [job_id1, job_id2]:
        priority = atf.get_job_parameter(jid, "Priority")
        reason = atf.get_job_parameter(jid, "Reason")
        assert (
            int(priority) == 0
        ), f"Expected Priority=0 after hold for job {jid}, got {priority}"
        assert (
            "JobHeldAdmin" in reason
        ), f"Expected Reason=JobHeldAdmin for job {jid}, got {reason}"

    # Release both: swap id types (SLUID for job1, numeric for job2)
    atf.run_command(
        f"scontrol release {sluid1} {job_id2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for jid in [job_id1, job_id2]:
        priority = atf.get_job_parameter(jid, "Priority")
        assert (
            int(priority) > 0
        ), f"Expected Priority > 0 after release for job {jid}, got {priority}"


# scontrol suspend / resume
def test_suspend_resume_by_sluid():
    """Suspend and resume a job using its SLUID."""

    job_id = submit_running_job()
    sluid = get_and_assert_sluid(job_id)

    atf.run_command(
        f"scontrol suspend {sluid}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "SUSPENDED", fatal=True)

    atf.run_command(
        f"scontrol resume {sluid}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)


def test_suspend_resume_mixed_ids():
    """Suspend and resume two jobs using a mix of job_id and SLUID."""

    job_id1 = submit_running_job()
    job_id2 = submit_running_job()
    sluid2 = get_and_assert_sluid(job_id2)

    # Suspend both
    atf.run_command(
        f"scontrol suspend {job_id1} {sluid2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id1, "SUSPENDED", fatal=True)
    atf.wait_for_job_state(job_id2, "SUSPENDED", fatal=True)

    # Resume both (swap id types)
    sluid1 = get_and_assert_sluid(job_id1)
    atf.run_command(
        f"scontrol resume {sluid1} {job_id2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)
    atf.wait_for_job_state(job_id2, "RUNNING", fatal=True)


# scontrol requeue
def test_requeue_by_sluid():
    """Requeue a running job using its SLUID."""

    job_id = submit_requeueable_job()
    sluid = get_and_assert_sluid(job_id)

    atf.run_command(
        f"scontrol requeue {sluid}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)


def test_requeue_mixed_ids():
    """Requeue two running jobs using a mix of job_id and SLUID."""

    job_id1 = submit_requeueable_job()
    job_id2 = submit_requeueable_job()
    sluid2 = get_and_assert_sluid(job_id2)

    atf.run_command(
        f"scontrol requeue {job_id1} {sluid2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id1, "PENDING", fatal=True)
    atf.wait_for_job_state(job_id2, "PENDING", fatal=True)


def test_show_step_not_found():
    """Verify scontrol show step error messages use the requested id format.

    - Numeric: Job step 123.2 not found
    - SLUID: Job step s4SNKN4BQEWT00.2 not found
    - Array: Job step 123_5.2 not found (always numeric, SLUIDs can't have arrays)
    """

    job_id = submit_running_job()
    sluid = get_and_assert_sluid(job_id)

    # Numeric job_id
    result = atf.run_command(f"scontrol show step {job_id}.2")
    assert re.search(
        rf"Job step {job_id}\.2 not found", result["stdout"]
    ), f"Expected 'Job step {job_id}.2 not found' in output: {result['stdout']}"

    # SLUID
    result = atf.run_command(f"scontrol show step {sluid}.2")
    assert re.search(
        rf"Job step {re.escape(sluid)}\.2 not found", result["stdout"]
    ), f"Expected 'Job step {sluid}.2 not found' in output: {result['stdout']}"


def test_show_step_not_found_array():
    """Verify scontrol show step error for array jobs uses numeric format."""

    job_id = atf.submit_job_sbatch(
        "-n1 --array=0-1 --wrap 'srun sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    result = atf.run_command(f"scontrol show step {job_id}_0.2")
    assert re.search(
        rf"Job step {job_id}_0\.2 not found", result["stdout"]
    ), f"Expected 'Job step {job_id}_0.2 not found' in output: {result['stdout']}"


def test_show_job_json_sluid():
    """Verify scontrol --json show job contains sluid field."""

    job_id = submit_running_job()
    sluid = get_and_assert_sluid(job_id)

    output = atf.run_command_output(f"scontrol --json show job {job_id}", fatal=True)
    data = json.loads(output)
    jobs = data.get("jobs", [])
    assert len(jobs) == 1, f"Expected 1 job, got {len(jobs)}"

    job_sluid = jobs[0].get("step_id", {}).get("sluid", "")
    assert job_sluid == sluid, f"Expected sluid={sluid} in JSON, got {job_sluid}"


def test_show_step_json_sluid():
    """Verify scontrol --json show steps contains sluid field."""

    job_id = submit_running_job()
    sluid = get_and_assert_sluid(job_id)

    atf.wait_for_step(job_id, 0, fatal=True)

    output = atf.run_command_output(
        f"scontrol --json show steps {job_id}.0", fatal=True
    )
    data = json.loads(output)
    steps = data.get("steps", [])
    assert len(steps) >= 1, f"Expected at least 1 step, got {len(steps)}"

    step_sluid = steps[0].get("sluid", "")
    assert step_sluid == sluid, f"Expected sluid={sluid} in step JSON, got {step_sluid}"
