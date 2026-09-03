############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sbatch --wait (-W)."""

import re

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MinJobAge", 300, ">=")
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "script_exit, expected_states",
    [
        (0, ("COMPLETED", "COMPLETING")),
        (3, ("FAILED", "COMPLETING")),
    ],
)
def test_wait_normal_job(script_exit, expected_states):
    """sbatch -W blocks until the job finishes and forwards its exit code."""

    # The payload must outlive sbatch's first completion poll at t=2, or -W
    # returns without ever observing an unfinished job.
    result = atf.run_command(
        f'sbatch -N1 -o/dev/null -W --wrap "sleep 5; exit {script_exit}"',
        xfail=bool(script_exit),
    )

    job_match = re.search(r"Submitted \S+ job (\d+)", result["stdout"])
    assert job_match is not None, (
        f"sbatch did not report a submitted job (exit {result['exit_code']})."
        f" Output:\n{result['stdout']}\n{result['stderr']}"
    )

    # Add job to submitted-jobs in case this test fails so it can be cleaned up
    job_id = int(job_match.group(1))
    atf.properties["submitted-jobs"].append(job_id)

    assert result["exit_code"] == script_exit, (
        "sbatch -W should exit with the submitted job's exit code"
        f" ({script_exit}), not {result['exit_code']}"
    )

    # COMPLETING is allowed alongside the terminal states because
    # job_state_codes.html documents it as "job has finished or been cancelled
    # and is performing cleanup tasks" -- the job has terminated as --wait
    # promises, the epilog just has not finished.
    job_state = atf.get_job_parameter(job_id, "JobState")
    assert (
        job_state in expected_states
    ), f"sbatch should have waited for job {job_id} to finish, but it is in state {job_state}"


@pytest.mark.parametrize(
    "task_exits, expected_exit, expected_states",
    [
        ((3, 7, 1), 7, ("FAILED", "COMPLETING")),
        ((0, 0, 0), 0, ("COMPLETED", "COMPLETING")),
    ],
)
def test_wait_array_job(task_exits, expected_exit, expected_states):
    """sbatch -W waits for every array task, not just one.

    Regression test for a bug where -W tracked only one array task record
    internally (the one array task splitting off last, i.e. the
    highest-index task) instead of the whole array. In the mixed case task
    1 has the highest exit code (7) but is neither the slowest nor the last
    array index, so this only passes if sbatch waited for and considered
    every task's exit code, not just the last-finishing one or the
    highest-indexed one.
    """

    # Task 0 sleeps so the array is still running at sbatch's first poll and
    # the first task to complete exits 3 rather than the maximum 7.
    result = atf.run_command(
        "sbatch -N1 -o/dev/null -W --array=0-2 --wrap "
        "'case $SLURM_ARRAY_TASK_ID in "
        f"0) sleep 5; exit {task_exits[0]};; "
        f"1) exit {task_exits[1]};; "
        f"2) exit {task_exits[2]};; "
        "esac'",
        xfail=bool(expected_exit),
    )

    job_match = re.search(r"Submitted \S+ job (\d+)", result["stdout"])
    assert job_match is not None, (
        f"sbatch did not report a submitted job (exit {result['exit_code']})."
        f" Output:\n{result['stdout']}\n{result['stderr']}"
    )

    # Add job to submitted-jobs in case this test fails so it can be cleaned up
    array_job_id = int(job_match.group(1))
    atf.properties["submitted-jobs"].append(array_job_id)

    # sbatch.1 documents that for a job array, -W records the highest exit
    # code among all tasks.
    assert result["exit_code"] == expected_exit, (
        "sbatch -W for an array job should exit with the highest exit code"
        f" among all tasks ({expected_exit}), not {result['exit_code']}. This"
        " indicates -W did not wait for/see every array task."
    )

    jobs = atf.get_jobs(array_job_id)
    sorted_task_ids = sorted(j["ArrayTaskId"] for j in jobs.values())
    assert sorted_task_ids == [0, 1, 2], (
        f"Expected array job {array_job_id} to have tasks [0, 1, 2] after"
        f" sbatch -W returned, but scontrol reported {sorted_task_ids}"
    )
    for task_job_id, job_info in jobs.items():
        task_id = job_info["ArrayTaskId"]
        task_job_state = job_info["JobState"]
        assert task_job_state in expected_states, (
            f"sbatch should have waited for array task {task_id} (job"
            f" {task_job_id}) to finish, but it is in state {task_job_state}"
        )
