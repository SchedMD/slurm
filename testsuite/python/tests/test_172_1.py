############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify the exit code jobcomp/filetxt records for a completed job.

Unlike the client commands, jobcomp writes to a file a site is likely to
parse by machine. test_108_13 already pins the decode itself, so cover only
what is specific to this sink: the signal half of ExitCode, and
DerivedExitCode.
"""

import re

import pytest

import atf

# Raised over atf's 45s default: the record is written during job
# completion, which on a loaded runner trails the DONE state by the whole
# epilog.
DONE_WAIT_SECS = 120
RECORD_WAIT_SECS = 60


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("JobCompType", "jobcomp/filetxt")
    atf.require_config_parameter("JobCompLoc", f"{atf.module_tmp_path}/jobcomp.log")
    # jobcomp writes from the controller, and a stepmgr job's step exit codes
    # never reach the controller's derived_ec, so DerivedExitCode would record
    # 0:0 there no matter what the steps did.
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


def _jobcomp_log():
    """Path for JobCompLoc. slurmctld reads it and its working directory is
    not the test's, so this needs an absolute path rather than a relative
    one in the test's cwd."""

    return f"{atf.module_tmp_path}/jobcomp.log"


def _jobcomp_record(job_id):
    """The jobcomp/filetxt line for job_id, or None if it never appears."""

    # The log holds one line per completed job and the write is not
    # synchronous with the job reaching DONE, so poll for this job's own
    # record. Neither call is fatal: the file does not exist until some job
    # completes, and an exhausted poll is the caller's assertion to make.
    for _ in atf.timer(timeout=RECORD_WAIT_SECS, poll_interval=1, quiet=True):
        result = atf.run_command(f"cat {_jobcomp_log()}", quiet=True)
        for line in result["stdout"].splitlines():
            if f"JobId={job_id} " in line:
                return line

    return None


def _recorded_exit_code(record, field):
    """<status>:<signal> as jobcomp/filetxt renders the named field."""

    # \b keeps ExitCode from also matching inside DerivedExitCode.
    match = re.search(rf"\b{field}=(\d+:\d+)", record)
    assert match, f"no {field} in the jobcomp record: {record!r}"
    return match.group(1)


def test_jobcomp_filetxt_exit_code_rendering():
    """jobcomp/filetxt records ExitCode with the same <status>:<signal>
    split the client commands render.

    Only the signal case is exercised: it is the half that proves the
    helper picked the right branch, and a <status>:0 job would also pass
    under a naive implementation. test_108_13 covers the status half
    across scontrol, squeue and sacct.
    """

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_172_1 --wrap 'kill -KILL $$'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=DONE_WAIT_SECS, fatal=True)

    record = _jobcomp_record(job_id)
    assert record, f"no jobcomp record was written for job {job_id}"
    assert (
        _recorded_exit_code(record, "ExitCode") == "0:9"
    ), f"jobcomp ExitCode: {record!r}"


def test_jobcomp_filetxt_derived_exit_code_rendering():
    """DerivedExitCode goes through the same decode, and carries the step's
    code rather than the script's."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_172_1_derived "
        "--wrap 'srun -n1 /bin/sh -c \"exit 123\"; exit 0'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=DONE_WAIT_SECS, fatal=True)

    record = _jobcomp_record(job_id)
    assert record, f"no jobcomp record was written for job {job_id}"
    assert (
        _recorded_exit_code(record, "ExitCode") == "0:0"
    ), f"jobcomp ExitCode: {record!r}"
    assert (
        _recorded_exit_code(record, "DerivedExitCode") == "123:0"
    ), f"jobcomp DerivedExitCode: {record!r}"
