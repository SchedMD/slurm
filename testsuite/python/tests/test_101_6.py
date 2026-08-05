############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify sacct selects jobs by job name."""

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
        "bin/sacct",
        reason="Ticket 25236: sacct -J/--job-name was added in 26.11",
    )
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def jobs():
    job1 = atf.submit_job_sbatch(f"--job-name={name1} --wrap 'true'", fatal=True)
    job2 = atf.submit_job_sbatch(f"--job-name={name2} --wrap 'true'", fatal=True)

    atf.wait_for_job_accounted(job1, fatal=True)
    atf.wait_for_job_accounted(job2, fatal=True)

    return job1, job2


@pytest.mark.parametrize("opt", ["-J", "--job-name", "--name"])
def test_job_name_filter(jobs, opt):
    """Verify sacct name filters report only the matching job"""

    job1, job2 = jobs

    job_ids = []
    for _ in atf.timer():
        result = atf.run_command(
            f"sacct {opt} {name1} -X -P -n -o JobID --starttime=now-15minutes",
            quiet=True,
        )

        assert result["exit_code"] == 0, f"sacct should accept {opt}"

        job_ids = result["stdout"].split()
        if str(job1) in job_ids:
            break

    assert str(job1) in job_ids, f"sacct {opt} {name1} should report job {job1}"
    assert str(job2) not in job_ids, f"sacct {opt} {name1} should not report job {job2}"


@pytest.mark.parametrize("opt", ["-J", "--job-name", "--name"])
def test_job_name_list(jobs, opt):
    """Verify sacct name filters accept a comma separated name list"""

    job1, job2 = jobs

    job_ids = []
    for _ in atf.timer():
        result = atf.run_command(
            f"sacct {opt} {name1},{name2} -X -P -n -o JobID"
            " --starttime=now-15minutes",
            quiet=True,
        )

        assert result["exit_code"] == 0, f"sacct should accept {opt}"

        job_ids = result["stdout"].split()
        if str(job1) in job_ids and str(job2) in job_ids:
            break

    assert str(job1) in job_ids, f"sacct {opt} {name1},{name2} should report job {job1}"
    assert str(job2) in job_ids, f"sacct {opt} {name1},{name2} should report job {job2}"


@pytest.mark.parametrize("opt", ["-J", "--job-name", "--name"])
def test_job_name_list_partial_match(jobs, opt):
    """Verify sacct name filters report the subset whose names are used"""

    job1, _ = jobs

    job_ids = []
    for _ in atf.timer():
        result = atf.run_command(
            f"sacct {opt} {name1},{test_name}_absent -X -P -n -o JobID"
            " --starttime=now-15minutes",
            quiet=True,
        )

        assert result["exit_code"] == 0, f"sacct should accept {opt}"

        job_ids = result["stdout"].split()
        if str(job1) in job_ids:
            break

    assert job_ids == [
        str(job1)
    ], f"sacct {opt} should report only job {job1} when one name in the list is unused"


def test_job_id_option_prefix(jobs):
    """Verify sacct --job stays an exact match after --job-name was added

    Adding --job-name makes --job a non-unique prefix of --jobs, so --job
    resolves only through the explicit long_options entry that came with it.
    """

    job1, _ = jobs

    job_ids = atf.run_command_output(
        f"sacct --job={job1} -X -P -n -o JobID --starttime=now-15minutes", fatal=True
    ).split()

    assert job_ids == [str(job1)], f"sacct --job={job1} should select only job {job1}"
