############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""scontrol reports a job's Slurm return code, and only with --details.

SlurmRC is printed on the DerivedExitCode line, which scontrol emits only with
--details. The last test checks scontrol and sacct report the same ExitCode
and SlurmRC for one job.

These tests call scontrol directly because atf.get_job_parameter() always
passes -d.
"""

import re

import pytest

import atf

# Every Slurm error code is 1000 or greater; every exit status is below 256.
LOWEST_SLURM_ERROR_CODE = 1000

# A working directory the stepd cannot open a job's output file in, which is
# how these tests make Slurm fail a job before its script ever runs. Two
# properties are load-bearing:
#   - The job must use the DEFAULT output file. expand_stdio_fields() joins
#     this directory onto slurm-%j.out when the step record is built, so the
#     stepd opens an absolute path under it. An explicit --output would
#     disarm the primitive and the job would simply run.
#   - The directory must be one the stepd cannot CREATE, not merely one that
#     is absent: on ENOENT it calls mkdirpath() and retries the open, so a
#     missing directory under a writable parent is created and the job runs.
#     Nesting it under /dev/null gives ENOTDIR instead, on every node and
#     whichever user the job runs as.
UNUSABLE_WORK_DIR = "/dev/null/slurm_test_workdir"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


@pytest.fixture
def accounting():
    """Accounting and a 26.11 sacct, for the scontrol/sacct comparison only."""
    atf.require_accounting()
    atf.require_version(
        (26, 11),
        "bin/sacct",
        reason="Ticket 24195: the SlurmRC field was added in 26.11",
    )


requires_rc = pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Ticket 24195: the SlurmRC field was added in 26.11",
)


def _failed_job():
    """Submit a job that Slurm itself fails, and wait for it to finish.

    An unusable working directory makes the stepd fail while opening the
    job's output file, so the job never runs and Slurm records an error code
    for it.
    """
    job_id = atf.submit_job_sbatch(
        f"--no-requeue --chdir={UNUSABLE_WORK_DIR} --wrap 'exit 0'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    return job_id


@requires_rc
def test_rc_shown_only_with_details():
    """Verify scontrol show job prints SlurmRC only with --details"""

    job_id = atf.submit_job_sbatch("--no-requeue --wrap 'exit 3'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    plain = atf.run_command_output(f"scontrol show job {job_id}", fatal=True)
    assert re.search(r"\bExitCode=", plain), f"ExitCode missing: {plain}"
    assert not re.search(
        r"\bSlurmRC=", plain
    ), f"SlurmRC printed without --details: {plain}"

    details = atf.run_command_output(
        f"scontrol show job --details {job_id}", fatal=True
    )
    rcs = re.findall(r"\bSlurmRC=(\d+)", details)
    assert rcs == ["0"], f"--details should print SlurmRC=0 once: {details}"


@requires_rc
def test_details_reports_the_error_code_for_a_slurm_failure():
    """Verify SlurmRC carries the Slurm error code when Slurm failed the job"""

    job_id = _failed_job()

    output = atf.run_command_output(f"scontrol show job --details {job_id}", fatal=True)

    rc_match = re.search(r"\bSlurmRC=(\d+)", output)
    assert (
        rc_match is not None
    ), f"scontrol --details should print SlurmRC; got {output}"
    rc = int(rc_match.group(1))
    assert rc >= LOWEST_SLURM_ERROR_CODE, (
        f"A job Slurm failed should report a Slurm error code in SlurmRC, got {rc}. "
        f"Every Slurm error code is >= {LOWEST_SLURM_ERROR_CODE}, so a smaller "
        "value means an exit status was stored in SlurmRC."
    )

    exit_match = re.search(r"\bExitCode=(\d+:\d+)", output)
    assert exit_match is not None, f"scontrol should print ExitCode; got {output}"
    assert exit_match.group(1) == "1:0", (
        "A job Slurm failed should report ExitCode 1:0. A value of 0:1 means "
        "the exit status is being read as a signal; got "
        f"{exit_match.group(1)}"
    )


@requires_rc
def test_scontrol_and_sacct_agree(accounting):
    """Verify scontrol and sacct report the same ExitCode and SlurmRC

    They read the same two values from different sources. Comparing them on
    one job is the cheapest guard against them drifting apart.
    """

    job_id = _failed_job()
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    scontrol_out = atf.run_command_output(
        f"scontrol show job --details {job_id}", fatal=True
    )
    exit_match = re.search(r"\bExitCode=(\d+:\d+)", scontrol_out)
    assert exit_match is not None, f"scontrol should print ExitCode; got {scontrol_out}"
    rc_match = re.search(r"\bSlurmRC=(\d+)", scontrol_out)
    assert (
        rc_match is not None
    ), f"scontrol --details should print SlurmRC; got {scontrol_out}"
    scontrol_exit = exit_match.group(1)
    scontrol_rc = rc_match.group(1)

    sacct_out = atf.run_command_output(
        f"sacct -j {job_id} -X --noheader -P -o ExitCode,SlurmRC", fatal=True
    ).strip()
    sacct_exit, sacct_rc = sacct_out.split("|")

    assert scontrol_exit == sacct_exit, (
        f"scontrol and sacct disagree on ExitCode for job {job_id}: "
        f"{scontrol_exit} vs {sacct_exit}"
    )
    assert scontrol_rc == sacct_rc, (
        f"scontrol and sacct disagree on SlurmRC for job {job_id}: "
        f"{scontrol_rc} vs {sacct_rc}"
    )
