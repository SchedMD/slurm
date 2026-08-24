############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that SchedulerParameters=max_array_tasks caps the tasks in a job array.

max_array_tasks limits how many tasks one array may hold, independently of
MaxArraySize limiting the highest index. An array over the limit has to be
refused, and refused for the limit rather than for its syntax.
"""

import pytest

import atf

MAX_ARRAY_TASKS_ERROR = "Job Array exceeds the maximum number of tasks"

# Held below MaxArraySize so that the task count, not the index, is what
# rejects the array.
max_array_tasks = 4
max_array_size = 100

# Each holds exactly max_array_tasks tasks: a dense range, and a sparse one
# whose highest index is far above max_array_tasks, which is only accepted if
# the limit counts tasks rather than the highest index.
AT_LIMIT_ARRAYS = [
    pytest.param(f"0-{max_array_tasks - 1}", id="range"),
    pytest.param(
        f"0-{max_array_size - 1}:{max_array_size // max_array_tasks}",
        id="sparse",
    ),
]

# Each holds one task more than the limit: a dense range, and a sparse list
# that stays below the highest legal index so the task count is what rejects
# it rather than the index.
OVER_LIMIT_ARRAYS = [
    pytest.param(f"0-{max_array_tasks}", id="range"),
    pytest.param(
        ",".join(str(index) for index in range(0, 2 * (max_array_tasks + 1), 2)),
        id="sparse",
    ),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MaxArraySize", max_array_size)
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("max_array_tasks", max_array_tasks)
    )
    atf.require_slurm_running()


def sbatch_args(array):
    """Returns the sbatch arguments submitting a job array."""
    return f"--array='{array}' -N1 -o /dev/null --wrap 'sleep infinity'"


@pytest.mark.parametrize("array", AT_LIMIT_ARRAYS)
def test_at_max_array_tasks(array):
    """Test that an array holding exactly max_array_tasks tasks is accepted."""
    job_id = atf.submit_job_sbatch(sbatch_args(array))
    assert job_id, f"--array={array} should be accepted"


@pytest.mark.parametrize("array", OVER_LIMIT_ARRAYS)
def test_above_max_array_tasks(array):
    """Test that an array holding more than max_array_tasks is rejected."""
    result = atf.run_command(f"sbatch {sbatch_args(array)}", xfail=True)
    assert (
        result["exit_code"] != 0
    ), f"--array={array} should be rejected, but printed {result['stdout']!r}"


# The limit itself is not new, only the error naming it. sbatch renders the
# error from its own copy of the error table, so an older sbatch prints
# "Unknown error". slurmctld is never older than its client commands, so
# gating on sbatch also covers a slurmctld that still reports the generic
# invalid array.
@pytest.mark.skipif(
    atf.get_version("bin/sbatch") < (26, 11),
    reason="Ticket 25423: an array over max_array_tasks was reported as a generic invalid array before 26.11",
)
@pytest.mark.parametrize("array", OVER_LIMIT_ARRAYS)
def test_above_max_array_tasks_message(array):
    """Test that sbatch names the task limit rather than the syntax."""
    result = atf.run_command(f"sbatch {sbatch_args(array)}", xfail=True)
    assert (
        MAX_ARRAY_TASKS_ERROR in result["stderr"]
    ), f"sbatch should report {MAX_ARRAY_TASKS_ERROR!r}, but printed {result['stderr']!r}"
