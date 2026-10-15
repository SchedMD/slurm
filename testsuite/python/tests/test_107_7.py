############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify scancel rejects an invalid job array expression.

Scripts branch on scancel's exit status, so an expression that cancels
nothing must be reported as a failure rather than as a successful no-op,
and must signal no task.
"""

import pytest

import atf

# Before 26.11 scancel exited zero for these: "_[0-]" cancelled task 0, and
# the others were treated like an already finished job. slurmctld decides
# whether these are rejected, so an older scancel rejects them too once
# slurmctld is 26.11.
previously_accepted = pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Ticket 25423: scancel exited zero for these array expressions before 26.11",
)

# Expressions that violate the documented array task id syntax. "{max}" is
# replaced with MaxArraySize.
INVALID_ARRAY_EXPRS = [
    pytest.param("_[3-2]", id="end-before-start"),
    pytest.param("_[0-10:0]", id="zero-step"),
    pytest.param("_-1", id="negative"),
    pytest.param("_abc", id="non-numeric"),
    pytest.param("_[]", id="empty-brackets"),
    pytest.param("_", id="empty", marks=previously_accepted),
    pytest.param("_[0-]", id="range-without-end", marks=previously_accepted),
    pytest.param("_{max}", id="too-large", marks=previously_accepted),
    pytest.param("_[{max}]", id="too-large-bracketed", marks=previously_accepted),
]

# scancel.1 documents the same handling of an array_id with and without --ctld.
SCANCEL_MODES = ["", "--ctld"]

# Before 26.11 --ctld rejected some of INVALID_ARRAY_EXPRS and silently accepted
# others, so those cases cannot be stated as one expectation and are skipped
# there. Only tests that parametrize over INVALID_ARRAY_EXPRS need to skip.
INVALID_SCANCEL_MODES = [
    "",
    pytest.param(
        "--ctld",
        marks=pytest.mark.skipif(
            atf.get_version("bin/scancel") < (26, 11),
            reason="Ticket 25423: scancel --ctld rejected these expressions inconsistently before 26.11",
        ),
    ),
]

array_size = 3


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MaxArraySize", array_size, ">=")
    atf.require_slurm_running()


@pytest.fixture
def array_job_id():
    """Submits a job array and returns its job id."""
    job_id = atf.submit_job_sbatch(
        f"-a 0-{array_size - 1} -t5 --hold --wrap 'sleep infinity'",
        fatal=True,
    )
    yield job_id


def scancel_command(mode, job_id_str):
    """Returns the scancel command for cancelling job_id_str with mode."""
    mode_str = f"{mode} " if mode else ""
    return f"scancel {mode_str}{job_id_str}"


def array_task_states(job_id):
    """Returns a dict of array task id to state for the tasks of a job array."""
    output = atf.run_command_output(
        f"squeue --noheader --jobs={job_id} --array --states=all "
        "--Format=ArrayTaskId,State",
        fatal=True,
    ).split()
    return dict(zip(output[::2], output[1::2]))


def assert_array_intact(job_id):
    """Asserts that every task of the job array still exists."""
    states = array_task_states(job_id)
    assert sorted(states) == sorted(
        str(task_id) for task_id in range(array_size)
    ), f"Every task of {job_id} should still exist, but found {states}"


@pytest.mark.parametrize("mode", INVALID_SCANCEL_MODES)
@pytest.mark.parametrize("expr", INVALID_ARRAY_EXPRS)
def test_invalid_array_expression(array_job_id, expr, mode):
    """Test that an invalid array expression is refused and cancels no task."""
    # slurm.conf documents the maximum index as one less than MaxArraySize.
    max_array_size = atf.get_config_parameter("MaxArraySize")
    job_id_str = f"{array_job_id}{expr.format(max=max_array_size)}"
    command = scancel_command(mode, job_id_str)
    assert (
        atf.run_command_exit(command, xfail=True) != 0
    ), f"{command} should exit non-zero"
    assert_array_intact(array_job_id)


@pytest.mark.parametrize("mode", SCANCEL_MODES)
def test_max_index_expression(mode):
    """Test that scancel accepts and cancels the highest legal task index."""
    max_index = int(atf.get_config_parameter("MaxArraySize")) - 1
    job_id = atf.submit_job_sbatch(
        f"-a 0,{max_index} -t5 --hold --wrap 'sleep infinity'", fatal=True
    )
    command = scancel_command(mode, f"{job_id}_{max_index}")
    assert atf.run_command_exit(command) == 0, f"{command} should exit zero"
    assert sorted(array_task_states(job_id)) == [
        "0"
    ], f"Only task {max_index} of {job_id} should have been cancelled"


@pytest.mark.parametrize("mode", SCANCEL_MODES)
def test_valid_array_expression(array_job_id, mode):
    """Test that scancel still cancels a task for a valid array expression."""
    cancelled = array_size - 1
    job_id_str = f"{array_job_id}_{cancelled}"
    command = scancel_command(mode, job_id_str)
    assert atf.run_command_exit(command) == 0, f"{command} should exit zero"
    assert sorted(array_task_states(array_job_id)) == sorted(
        str(task_id) for task_id in range(cancelled)
    ), f"Only task {cancelled} of {array_job_id} should have been cancelled"


@pytest.mark.parametrize("mode", SCANCEL_MODES)
def test_finished_task_expression(mode):
    """Test that scancel treats naming an already finished task as no error."""
    job_id = atf.submit_job_sbatch(
        f"-a 0-{array_size - 1} -t1 -o /dev/null --wrap true", fatal=True
    )
    for _ in atf.timer(fatal=True):
        states = array_task_states(job_id)
        if len(states) == array_size and all(
            state == "COMPLETED" for state in states.values()
        ):
            break
    command = scancel_command(mode, f"{job_id}_{array_size - 1}")
    assert atf.run_command_exit(command) == 0, f"{command} should exit zero"


@pytest.mark.parametrize("mode", SCANCEL_MODES)
def test_absent_task_expression(mode):
    """Test that scancel treats naming a task never in the array as no error."""
    job_id = atf.submit_job_sbatch(
        "-a 0,2 -t5 --hold --wrap 'sleep infinity'", fatal=True
    )
    command = scancel_command(mode, f"{job_id}_1")
    assert atf.run_command_exit(command) == 0, f"{command} should exit zero"
    assert sorted(array_task_states(job_id)) == [
        "0",
        "2",
    ], f"{command} should leave every task of {job_id}"


@pytest.mark.parametrize("mode", SCANCEL_MODES)
@pytest.mark.parametrize(
    "expr,remaining",
    [
        pytest.param("_[0-1]", ["2"], id="range"),
        pytest.param("_[0,2]", ["1"], id="list"),
    ],
)
def test_bracketed_array_expression(array_job_id, expr, remaining, mode):
    """Test that scancel cancels exactly the tasks a bracketed expression names."""
    command = scancel_command(mode, f"{array_job_id}{expr}")
    assert atf.run_command_exit(command) == 0, f"{command} should exit zero"
    assert (
        sorted(array_task_states(array_job_id)) == remaining
    ), f"{command} should leave only task {remaining} of {array_job_id}"
