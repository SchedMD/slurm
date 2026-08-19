############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that sbatch rejects an invalid --array specification.

A specification that selects no task, or that contains an invalid index or
range alongside valid ones, must be refused rather than submitted as some
other job array.
"""

import pytest

import atf

# Before 26.11 these were accepted and a job submitted anyway: a job array
# holding a single task 0 or no task at all, or for an empty value a plain
# non-array job. slurmctld decides whether these are rejected, so an older
# sbatch has them rejected too once slurmctld is 26.11.
previously_accepted = pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Ticket 25423: non-numeric and empty --array inputs were accepted before 26.11",
)

NON_NUMERIC = "Job Array ID includes unexpected non-numeric characters"
BAD_STEP = "Job Array ID step size must be greater than zero and less than MaxArraySize"

# Specifications that violate the documented --array syntax, with the error
# each one must report. "{max}" becomes MaxArraySize, which is one above the
# maximum index.
INVALID_ARRAYS = [
    pytest.param(
        "3-2",
        "Job Array ID range end can not be less than its start",
        id="end-before-start",
    ),
    pytest.param("0-10:0", BAD_STEP, id="zero-step"),
    pytest.param("0-1:{max}", BAD_STEP, id="step-too-large"),
    pytest.param("0:1", NON_NUMERIC, id="step-without-range"),
    pytest.param("abc", NON_NUMERIC, id="non-numeric"),
    pytest.param("-1", "Job Array ID can not be a negative number", id="negative"),
    pytest.param("{max}", "Job Array ID larger than acceptable range", id="too-large"),
    pytest.param("0,abc", NON_NUMERIC, id="list-with-non-numeric"),
    pytest.param(
        "0-2,5-3",
        "Job Array ID range end can not be less than its start",
        id="list-with-end-before-start",
    ),
    pytest.param(
        "0,{max}",
        "Job Array ID larger than acceptable range",
        id="list-with-too-large",
    ),
    pytest.param(
        ",",
        "Job Array ID must not be an empty string",
        id="separator-only",
        marks=previously_accepted,
    ),
    pytest.param("%2", NON_NUMERIC, id="throttle-only", marks=previously_accepted),
    pytest.param("0-", NON_NUMERIC, id="range-without-end", marks=previously_accepted),
    pytest.param("[]", NON_NUMERIC, id="empty-brackets", marks=previously_accepted),
    pytest.param("]", NON_NUMERIC, id="bracket-only", marks=previously_accepted),
    # A job script reaches this by passing an unset variable.
    pytest.param(
        "",
        "Job Array ID must not be an empty string",
        id="empty",
        marks=previously_accepted,
    ),
]

# The same specifications without their messages, for the tests that only need
# the rejection.
INVALID_ARRAY_SPECS = [
    pytest.param(param.values[0], marks=param.marks, id=param.id)
    for param in INVALID_ARRAYS
]

# Specifications that must keep working, with the task ids each one selects.
# "{max_step}" becomes one less than MaxArraySize.
VALID_ARRAYS = [
    pytest.param("0-2", [0, 1, 2], id="range"),
    pytest.param("0,2", [0, 2], id="list"),
    pytest.param("0-2:2", [0, 2], id="step"),
    pytest.param("0-1:{max_step}", [0], id="max_step"),
    pytest.param("0-2%2", [0, 1, 2], id="throttle"),
]

# The valid specifications above use task indexes up to 2.
min_array_size = 3


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MaxArraySize", min_array_size, ">=")
    atf.require_slurm_running()


def sbatch_args(array):
    """Returns the sbatch arguments submitting a job array."""
    return f"--array='{array}' -N1 -o /dev/null --wrap 'sleep infinity'"


def squeue_job_ids(user=atf.properties["test-user"]):
    """Returns the job id of each array element of a user."""
    output = atf.run_command_output(
        f"squeue --noheader --array --states=all --Format=JobId --all --user={user}",
        fatal=True,
    )
    return set(output.split())


def array_task_ids(job_id):
    """Returns the sorted task ids of a job array."""
    output = atf.run_command_output(
        "squeue --noheader --array --states=all --Format=ArrayTaskId "
        f"--jobs={job_id}",
        fatal=True,
    )
    return sorted(int(task_id) for task_id in output.split())


@pytest.mark.parametrize("array", INVALID_ARRAY_SPECS)
def test_invalid_array(array):
    """Test that sbatch rejects an invalid --array specification."""
    array = array.format(max=atf.get_config_parameter("MaxArraySize"))
    job_ids = squeue_job_ids()
    result = atf.run_command(f"sbatch {sbatch_args(array)}", xfail=True)
    assert (
        result["exit_code"] != 0
    ), f"sbatch --array={array} should exit non-zero, but printed {result['stdout']!r}"
    assert not (
        squeue_job_ids() - job_ids
    ), f"sbatch --array={array} should not have queued any task"


# sbatch renders the error itself, from its own copy of the error table, so an
# older sbatch prints "Unknown error" for the codes added in 26.11.
@pytest.mark.skipif(
    atf.get_version("bin/sbatch") < (26, 11),
    reason="Ticket 25423: an invalid array specification was reported as a generic invalid array before 26.11",
)
@pytest.mark.parametrize("array,message", INVALID_ARRAYS)
def test_invalid_array_message(array, message):
    """Test that sbatch names why an --array specification was rejected."""
    array = array.format(max=atf.get_config_parameter("MaxArraySize"))
    result = atf.run_command(f"sbatch {sbatch_args(array)}", xfail=True)
    assert (
        message in result["stderr"]
    ), f"sbatch --array={array} should report {message!r}, but printed {result['stderr']!r}"


@pytest.mark.parametrize("array,task_ids", VALID_ARRAYS)
def test_valid_array(array, task_ids):
    """Test that sbatch submits a valid --array specification."""
    max_step = int(atf.get_config_parameter("MaxArraySize")) - 1
    array = array.format(max_step=max_step)
    job_id = atf.submit_job_sbatch(sbatch_args(array), fatal=True)
    assert (
        array_task_ids(job_id) == task_ids
    ), f"sbatch --array={array} should submit tasks {task_ids}"
