############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify scontrol rejects an invalid job array expression.

An expression that selects no task must be refused without changing any
task, and must say why rather than blame the job id as a whole.
"""

import pytest

import atf

# Expressions that violate the documented array task id syntax: a range whose
# end precedes its start, a zero step, and an index above the maximum, with and
# without brackets. "{max}" becomes MaxArraySize, which is one above the
# maximum index.
INVALID_ARRAY_EXPRS = ["_[3-2]", "_[0-10:0]", "_[{max}]", "_{max}"]

# Expressions with the error each one must report.
INVALID_ARRAY_MESSAGES = [
    ("_[3-2]", "Job Array ID range end can not be less than its start"),
    ("_[{max}]", "Job Array ID larger than acceptable range"),
    ("_{max}", "Job Array ID larger than acceptable range"),
]

UPDATE_OPERATION = "update JobId={job_id_str} TimeLimit=1"
REQUEUE_OPERATION = "requeue {job_id_str}"
SUSPEND_OPERATION = "suspend {job_id_str}"
RESUME_OPERATION = "resume {job_id_str}"

# Only a privileged user or coordinator may suspend and resume a job, not its
# owner, so those run as SlurmUser to reach the array expression rather than
# stop at the access check.
ADMIN_OPERATIONS = [SUSPEND_OPERATION, RESUME_OPERATION]
REJECTING_OPERATIONS = [
    UPDATE_OPERATION,
    REQUEUE_OPERATION,
    SUSPEND_OPERATION,
    RESUME_OPERATION,
]
ACCEPTING_OPERATIONS = [UPDATE_OPERATION, REQUEUE_OPERATION]

array_size = 3


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MaxArraySize", array_size, ">=")
    atf.require_slurm_running()


@pytest.fixture
def array_job_id():
    """Submits a job array and returns its job id."""
    return atf.submit_job_sbatch(
        f"-a 0-{array_size - 1} -t5 --hold --wrap 'sleep infinity'",
        fatal=True,
    )


def scontrol_command(operation, job_id_str):
    """Returns the scontrol command applying operation to job_id_str."""
    return f"scontrol {operation.format(job_id_str=job_id_str)}"


def array_task_time_limits(job_id):
    """Returns dict from task ids to the time limits of a job array."""
    output = atf.run_command_output(
        f"squeue --noheader --array --states=all --Format=ArrayTaskId,TimeLimit "
        f"--jobs={job_id}",
        fatal=True,
    ).strip()
    limits = {}
    for line in output.split("\n"):
        if line:
            task_id, limit = line.split()
            limits[int(task_id)] = limit
    return limits


# Only update is checked here. The array is held, so requeue, suspend and
# resume refuse any of its tasks whatever the expression.
@pytest.mark.parametrize("expr", INVALID_ARRAY_EXPRS)
def test_invalid_array_expression(array_job_id, expr):
    """Test that an invalid array expression is rejected and changes no task."""
    max_array_size = atf.get_config_parameter("MaxArraySize")
    job_id_str = f"{array_job_id}{expr.format(max=max_array_size)}"
    before = array_task_time_limits(array_job_id)
    command = scontrol_command(UPDATE_OPERATION, job_id_str)
    assert (
        atf.run_command_exit(command, xfail=True) != 0
    ), f"{command} should exit non-zero"
    assert (
        array_task_time_limits(array_job_id) == before
    ), f"{command} should have changed no task"


# slurmctld picks the error but scontrol renders it from its own copy of the
# error table, so scontrol has to be new enough to expect the specific message.
# slurmctld is never older than its client commands, so this covers it too.
@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Ticket 25423: an invalid array expression was reported as a generic invalid job id before 26.11",
)
@pytest.mark.parametrize("operation", REJECTING_OPERATIONS)
@pytest.mark.parametrize("expr,message", INVALID_ARRAY_MESSAGES)
def test_invalid_array_expression_message(array_job_id, expr, message, operation):
    """Test that scontrol names why an array expression was rejected."""
    max_array_size = atf.get_config_parameter("MaxArraySize")
    job_id_str = f"{array_job_id}{expr.format(max=max_array_size)}"
    command = scontrol_command(operation, job_id_str)
    user = None
    if operation in ADMIN_OPERATIONS:
        user = atf.properties["slurm-user"]
    result = atf.run_command(command, user=user, xfail=True)
    assert (
        message in result["stderr"]
    ), f"{command} should report {message!r}, but printed {result['stderr']!r}"


@pytest.mark.parametrize("operation", ACCEPTING_OPERATIONS)
def test_valid_array_expression(array_job_id, operation):
    """Test that a valid array expression is not rejected for its syntax.

    Only the parse is under test here. An operation can still refuse a task
    for its own reasons, such as requeueing one that is still held, so this
    asserts the array errors are absent rather than that the command succeeds.
    """
    command = scontrol_command(operation, f"{array_job_id}_0")
    stderr = atf.run_command_error(command, xfail=True)
    assert (
        "Job Array ID" not in stderr
    ), f"{command} should not report an array id error, but printed {stderr!r}"


def test_valid_array_expression_applies(array_job_id):
    """Test that scontrol update changes only the task a valid expression names."""
    before = array_task_time_limits(array_job_id)
    command = scontrol_command(UPDATE_OPERATION, f"{array_job_id}_0")
    assert atf.run_command_exit(command) == 0, f"{command} should exit zero"
    after = array_task_time_limits(array_job_id)
    assert after[0] != before[0], f"{command} should have changed task 0"
    assert {t: v for t, v in after.items() if t != 0} == {
        t: v for t, v in before.items() if t != 0
    }, f"{command} should have changed no other task"
