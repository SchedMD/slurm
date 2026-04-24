############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5), "bin/sacct", reason="Ticket 22180: SLUID availability added in 26.05+"
    )
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def multistep_job():
    """Submit a batch job with 2 parallel steps and return (job_id, sluid)."""
    script = atf.module_tmp_path / "multistep.sh"
    atf.make_bash_script(
        script,
        """srun -n1 --mem=0 --overlap sleep infinity &
        srun -n1 --mem=0 --overlap sleep infinity &
        wait""",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -O {script}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)

    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid is not None, f"Job {job_id} has no SLUID"

    return job_id, sluid


def test_sstat_by_sluid(multistep_job):
    """Verify sstat -j <SLUID> shows steps and prints the SLUID field."""

    job_id, sluid = multistep_job
    fmt = "--noheader -o SLUID,JobID"

    output = atf.run_command_output(f"sstat -j {sluid} {fmt}", fatal=True)
    # Should show at least step 0
    assert re.search(
        rf"{re.escape(sluid)}\s+{job_id}\.", output
    ), f"Expected SLUID and job steps in output: {output}"


def test_sstat_by_sluid_step(multistep_job):
    """Verify sstat -j <SLUID>.1 shows only step 1."""

    job_id, sluid = multistep_job
    fmt = "--noheader -o SLUID,JobID"

    output = atf.run_command_output(f"sstat -j {sluid}.1 {fmt}", fatal=True)

    assert re.search(
        rf"{re.escape(sluid)}\s+{job_id}\.1", output
    ), f"Expected SLUID with step 1 in output: {output}"
    assert not re.search(
        rf"{job_id}\.0", output
    ), f"Step 0 should not appear when filtering by step 1: {output}"


def test_sstat_mixed_ids(multistep_job):
    """Verify sstat -j <job_id>.0,<SLUID>.1 shows both steps."""

    job_id, sluid = multistep_job
    fmt = "--noheader -o SLUID,JobID"

    output = atf.run_command_output(f"sstat -j {job_id}.0,{sluid}.1 {fmt}", fatal=True)
    assert re.search(rf"{job_id}\.0", output), f"Expected step 0 in output: {output}"
    assert re.search(rf"{job_id}\.1", output), f"Expected step 1 in output: {output}"


def test_sstat_nonexistent_step(multistep_job):
    """Verify sstat error messages for non-existent steps use the requested id format."""

    job_id, sluid = multistep_job

    result = atf.run_command(f"sstat -j {job_id}.2", xfail=True)
    assert re.search(
        rf"StepId={job_id}\.2 not found running", result["stderr"]
    ), f"Expected 'StepId={job_id}.2 not found running' in stderr: {result['stderr']}"

    result = atf.run_command(f"sstat -j {sluid}.2", xfail=True)
    assert re.search(
        rf"StepId={re.escape(sluid)}\.2 not found running", result["stderr"]
    ), f"Expected 'StepId={sluid}.2 not found running' in stderr: {result['stderr']}"
