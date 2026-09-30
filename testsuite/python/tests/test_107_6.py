############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify scancel restricts cancellation by job name."""

import os

import pexpect
import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
name1 = f"{test_name}_job1"
name2 = f"{test_name}_job2"
name3 = f"{test_name}_job3"

requires_j = pytest.mark.skipif(
    atf.get_version("bin/scancel") < (26, 11),
    reason="Ticket 25236: scancel -J/--job-name was added in 26.11",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
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


@pytest.mark.parametrize(
    "opt",
    [
        pytest.param("-J", marks=requires_j),
        pytest.param("--job-name", marks=requires_j),
        "--jobname",
        "--name",
        "-n",
    ],
)
def test_job_name_filter(jobs, opt):
    """Verify scancel name filters cancel only the matching job"""

    job1, job2 = jobs

    result = atf.run_command(f"scancel {opt} {name1}")

    assert result["exit_code"] == 0, f"scancel should accept {opt}"

    atf.wait_for_job_state(job1, "CANCELLED", fatal=True)
    assert (
        atf.get_job_parameter(job2, "JobState", fatal=True) == "PENDING"
    ), f"scancel {opt} {name1} should not have cancelled job {job2}"


@pytest.mark.parametrize(
    "names", [name1.upper(), f"{name1},{name2}"], ids=["case", "list"]
)
@pytest.mark.parametrize("opt", [pytest.param("-J", marks=requires_j), "--name"])
def test_job_name_exact_match(jobs, opt, names):
    """Verify scancel matches one name exactly, with no case folding or list"""

    job1, job2 = jobs

    atf.run_command(f"scancel {opt} {names}")

    for job in (job1, job2):
        assert (
            atf.get_job_parameter(job, "JobState", fatal=True) == "PENDING"
        ), f"scancel {opt} {names} should not have cancelled job {job}"


@pytest.fixture
def same_name_jobs():
    running = atf.submit_job_sbatch(
        f"--job-name={name3} --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(running, "RUNNING", fatal=True)

    pending = atf.submit_job_sbatch(
        f"-H --job-name={name3} --wrap 'sleep infinity'", fatal=True
    )

    return running, pending


@pytest.mark.parametrize("opt", [pytest.param("-J", marks=requires_j), "--name"])
def test_job_name_with_state_filter(same_name_jobs, opt):
    """Verify scancel signals only jobs satisfying every supplied filter"""

    running, pending = same_name_jobs

    result = atf.run_command(f"scancel {opt} {name3} --state=PENDING")

    assert result["exit_code"] == 0, f"scancel should accept {opt} with --state"

    atf.wait_for_job_state(pending, "CANCELLED", fatal=True)
    assert (
        atf.get_job_parameter(running, "JobState", fatal=True) == "RUNNING"
    ), f"scancel {opt} {name3} --state=PENDING should not have cancelled {running}"


@pytest.fixture
def interactive_scancel(request, jobs):
    child = atf.run_command_pexpect(f"scancel {request.param} {name1} --interactive")

    yield request.param, child

    child.close()


@pytest.mark.parametrize(
    "interactive_scancel",
    [pytest.param("-J", marks=requires_j), "--name"],
    indirect=True,
)
def test_job_name_interactive(jobs, interactive_scancel):
    """Verify the name filter narrows the --interactive prompt to one job"""

    job1, job2 = jobs
    opt, child = interactive_scancel

    index = child.expect(
        [rf"Cancel job_id={job1} .*\[y/n\]\? ", pexpect.TIMEOUT, pexpect.EOF]
    )
    assert index == 0, f"scancel {opt} {name1} --interactive should prompt for {job1}"

    child.sendline("y")

    index = child.expect([pexpect.EOF, pexpect.TIMEOUT])
    assert index == 0, f"scancel should not prompt again after cancelling job {job1}"

    child.close()
    assert child.exitstatus == 0, "scancel --interactive should exit 0"

    atf.wait_for_job_state(job1, "CANCELLED", fatal=True)
    assert (
        atf.get_job_parameter(job2, "JobState", fatal=True) == "PENDING"
    ), f"scancel {opt} {name1} --interactive should not have cancelled job {job2}"


def test_job_name_env(jobs):
    """Verify SCANCEL_NAME cancels only the matching job"""

    job1, job2 = jobs

    result = atf.run_command("scancel", env_vars=f"SCANCEL_NAME={name1}")

    assert result["exit_code"] == 0, "scancel should accept SCANCEL_NAME"

    atf.wait_for_job_state(job1, "CANCELLED", fatal=True)
    assert (
        atf.get_job_parameter(job2, "JobState", fatal=True) == "PENDING"
    ), f"SCANCEL_NAME={name1} should not have cancelled job {job2}"


@pytest.mark.parametrize("opt", [pytest.param("-J", marks=requires_j), "--name"])
def test_job_name_overrides_env(jobs, opt):
    """Verify a name option on the command line overrides SCANCEL_NAME"""

    job1, job2 = jobs

    result = atf.run_command(f"scancel {opt} {name2}", env_vars=f"SCANCEL_NAME={name1}")

    assert result["exit_code"] == 0, f"scancel should accept {opt} with SCANCEL_NAME"

    atf.wait_for_job_state(job2, "CANCELLED", fatal=True)
    assert (
        atf.get_job_parameter(job1, "JobState", fatal=True) == "PENDING"
    ), f"{opt} {name2} should override SCANCEL_NAME={name1}"
