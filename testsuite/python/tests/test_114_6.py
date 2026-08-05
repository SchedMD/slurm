############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify squeue selects jobs by job name."""

import os

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
name1 = f"{test_name}_job1"
name2 = f"{test_name}_job2"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/squeue",
        reason="Ticket 25236: squeue -J/--job-name was added in 26.11",
    )
    atf.require_slurm_running()


@pytest.fixture
def jobs():
    job1 = atf.submit_job_sbatch(
        f"-H --job-name={name1} --wrap 'sleep infinity'", fatal=True
    )
    job2 = atf.submit_job_sbatch(
        f"-H --job-name={name2} --wrap 'sleep infinity'", fatal=True
    )

    return job1, job2


@pytest.mark.parametrize("opt", ["-J", "--job-name", "--name", "-n"])
def test_job_name_filter(jobs, opt):
    """Verify squeue name filters select only the matching job"""

    job1, job2 = jobs

    result = atf.run_command(f"squeue {opt} {name1} --noheader --format=%i")

    assert result["exit_code"] == 0, f"squeue should accept {opt}"

    job_ids = result["stdout"].split()

    assert str(job1) in job_ids, f"squeue {opt} {name1} should list job {job1}"
    assert str(job2) not in job_ids, f"squeue {opt} {name1} should not list job {job2}"


@pytest.mark.parametrize("opt", ["-J", "--job-name", "--name", "-n"])
def test_job_name_case_insensitive(jobs, opt):
    """Verify squeue name filters match names case insensitively"""

    job1, job2 = jobs
    upper = name1.upper()

    result = atf.run_command(f"squeue {opt} {upper} --noheader --format=%i")

    assert result["exit_code"] == 0, f"squeue should accept {opt}"

    job_ids = result["stdout"].split()

    assert str(job1) in job_ids, f"squeue {opt} {upper} should list job {job1}"
    assert str(job2) not in job_ids, f"squeue {opt} {upper} should not list job {job2}"


@pytest.mark.parametrize("opt", ["-J", "--job-name", "--name", "-n"])
def test_job_name_list(jobs, opt):
    """Verify squeue name filters accept a comma separated name list"""

    job1, job2 = jobs

    result = atf.run_command(f"squeue {opt} {name1},{name2} --noheader --format=%i")

    assert result["exit_code"] == 0, f"squeue should accept {opt}"

    job_ids = result["stdout"].split()

    assert str(job1) in job_ids, f"squeue {opt} {name1},{name2} should list job {job1}"
    assert str(job2) in job_ids, f"squeue {opt} {name1},{name2} should list job {job2}"

    result = atf.run_command(
        f"squeue {opt} {name1},{test_name}_absent --noheader --format=%i"
    )

    assert result["exit_code"] == 0, f"squeue should accept {opt}"

    job_ids = result["stdout"].split()

    assert job_ids == [
        str(job1)
    ], f"squeue {opt} should select only job {job1} when one name in the list is unused"


def test_job_name_env(jobs):
    """Verify SQUEUE_NAMES selects only the matching job"""

    job1, job2 = jobs

    result = atf.run_command(
        "squeue --noheader --format=%i", env_vars=f"SQUEUE_NAMES={name1}"
    )

    assert result["exit_code"] == 0, "squeue should accept SQUEUE_NAMES"

    job_ids = result["stdout"].split()

    assert str(job1) in job_ids, f"SQUEUE_NAMES={name1} should list job {job1}"
    assert str(job2) not in job_ids, f"SQUEUE_NAMES={name1} should not list job {job2}"


def test_job_name_env_list(jobs):
    """Verify SQUEUE_NAMES accepts a comma separated name list"""

    job1, job2 = jobs

    result = atf.run_command(
        "squeue --noheader --format=%i", env_vars=f"SQUEUE_NAMES={name1},{name2}"
    )

    assert result["exit_code"] == 0, "squeue should accept SQUEUE_NAMES"

    job_ids = result["stdout"].split()

    assert str(job1) in job_ids, f"SQUEUE_NAMES={name1},{name2} should list job {job1}"
    assert str(job2) in job_ids, f"SQUEUE_NAMES={name1},{name2} should list job {job2}"


def test_job_name_overrides_env(jobs):
    """Verify -J on the command line overrides SQUEUE_NAMES"""

    job1, job2 = jobs

    result = atf.run_command(
        f"squeue -J {name2} --noheader --format=%i",
        env_vars=f"SQUEUE_NAMES={name1}",
    )

    assert result["exit_code"] == 0, "squeue should accept -J with SQUEUE_NAMES set"

    job_ids = result["stdout"].split()

    assert str(job2) in job_ids, f"-J {name2} should override SQUEUE_NAMES={name1}"
    assert str(job1) not in job_ids, f"-J {name2} should override SQUEUE_NAMES={name1}"


@pytest.mark.parametrize("opt", ["-J", "--job-name", "--name", "-n"])
def test_job_name_no_match(jobs, opt):
    """Verify squeue name filters select nothing for an unused name"""

    result = atf.run_command(f"squeue {opt} {test_name}_absent --noheader --format=%i")

    assert result["exit_code"] == 0, f"squeue should accept {opt}"
    assert (
        result["stdout"].strip() == ""
    ), f"squeue {opt} {test_name}_absent should not list any job"


def test_job_id_option_prefix(jobs):
    """Verify squeue --job stays an exact match after --job-name was added

    Adding --job-name makes --job a non-unique prefix of --jobs, so --job
    resolves only through the explicit long_options entry that came with it.
    """

    job1, _ = jobs

    job_ids = atf.run_command_output(
        f"squeue --job={job1} --noheader --format=%i", fatal=True
    ).split()

    assert job_ids == [str(job1)], f"squeue --job={job1} should select only job {job1}"
