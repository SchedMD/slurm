############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""squeue renders a job's Slurm return code through --Format.

SlurmRC has no % code, so it is reachable through -O/--Format only. squeue
rejects an unknown field, so rendering a value proves SlurmRC is wired up.

A running job must report SlurmRC 0. A job Slurm failed must report the
same error code scontrol does. A finished job only shows up with
--states=all, which keeps it listed until MinJobAge expires.
"""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


# A working directory the stepd cannot open the default output file in, so
# Slurm fails the job before its script runs. See test_101_14 for why it
# sits under /dev/null.
UNUSABLE_WORK_DIR = "/dev/null/slurm_test_workdir"

requires_rc = pytest.mark.skipif(
    atf.get_version("bin/squeue") < (26, 11),
    reason="Ticket 24195: the SlurmRC format field was added in 26.11",
)


@requires_rc
def test_squeue_format_rc():
    """Verify squeue -O SlurmRC reports a running job's Slurm return code"""

    job_id = atf.submit_job_sbatch("--wrap 'srun sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    output = atf.run_command_output(
        f"squeue --noheader -O JobID,SlurmRC -j {job_id}", fatal=True
    ).strip()

    assert output, (
        f"squeue -O JobID,SlurmRC produced no output for job {job_id}. squeue "
        "rejects an unknown format field, so an empty result means SlurmRC is not "
        "registered."
    )
    assert re.match(rf"^{job_id}\s+0\s*$", output), (
        f"squeue -O JobID,SlurmRC should report SlurmRC 0 for a running job that Slurm "
        f"has not failed; got {output!r}"
    )


@requires_rc
@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Ticket 24195: scontrol reports SlurmRC since 26.11",
)
def test_squeue_format_rc_of_a_failed_job():
    """Verify squeue -O SlurmRC reports the error code of a job Slurm failed"""

    job_id = atf.submit_job_sbatch(
        f"--no-requeue --chdir={UNUSABLE_WORK_DIR} --wrap 'exit 0'", fatal=True
    )
    atf.wait_for_job_state(job_id, "FAILED", fatal=True)

    details = atf.run_command_output(
        f"scontrol show job --details {job_id}", fatal=True
    )
    match = re.search(r"\bSlurmRC=(\d+)", details)
    assert match, f"scontrol --details should print SlurmRC: {details}"
    rc = match.group(1)
    assert int(rc) != 0, f"A job Slurm failed should have a non-zero SlurmRC: {details}"

    output = atf.run_command_output(
        f"squeue --noheader --states=all -O JobID,SlurmRC -j {job_id}", fatal=True
    ).strip()
    assert re.match(rf"^{job_id}\s+{rc}\s*$", output), (
        f"squeue -O JobID,SlurmRC should report the SlurmRC scontrol does, {rc}, "
        f"for job {job_id}; got {output!r}"
    )
