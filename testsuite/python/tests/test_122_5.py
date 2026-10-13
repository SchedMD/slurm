############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scancel job array id handling."""

import logging
import re

import pytest

import atf

task_limit = 3


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Need to split array tasks
    atf.require_nodes(task_limit)
    # test_big_array submits a 1-1000 array, so index 1000 must be in range.
    atf.require_config_parameter("MaxArraySize", 1001, operator=">=")
    # scancel must reject an already-finished task as already-done rather than
    # as an invalid job id, which requires its job record to outlive the test.
    atf.require_config_parameter("MinJobAge", 300, operator=">=")
    atf.require_slurm_running()


def submit_job(array_spec, prog, extra=None):
    opts = [f"-n1 -c1 --array={array_spec}"]
    if extra:
        opts.append(extra)
    opts.append(f"--wrap '{prog}'")
    return atf.submit_job_sbatch(" ".join(opts), fatal=True)


def setup_array_state(ntasks):
    array_spec = f"1-{ntasks}%{task_limit}"
    job_id = submit_job(array_spec, "srun sleep infinity")
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    state = {
        "metajob_id": job_id,
        "pending_tasks": set(range(1, ntasks + 1)),
        "running": {},  # task_id: job_id
        # Tasks cancelled while running keep a terminal job record, so scancel
        # rejects them differently than tasks that never split from the array
        "terminated": set(),
    }
    resync_running(state)
    return state


def wait_for_jobs(job_list, expected_state):
    for job_id in job_list:
        atf.wait_for_job_state(job_id, expected_state, fatal=True)


def run_scancel(job_list, ctld=True, signal=None, **kwargs):
    cmd = ["scancel --verbose"]
    if ctld:
        cmd.append("--ctld")
    if signal:
        cmd.append(f"--signal={signal}")
    # Quoted to avoid shell globbing in a bracketed array id
    cmd.extend(f"'{job_id}'" for job_id in job_list)
    return atf.run_command(" ".join(cmd), **kwargs)


def parse_tasks(array_spec):
    task_ids = set()
    for task_range in array_spec.strip("[]").split(","):
        first, _, last = task_range.partition("-")
        assert first.isdigit() and (
            not last or last.isdigit()
        ), f"'{array_spec}' is not a plain task expression, was it truncated?"
        first = int(first)
        if not last:
            task_ids.add(first)
        else:
            task_ids.update(range(first, int(last) + 1))
    return task_ids


def get_array_task_states(job_id, states=None, **kwargs):
    """Return the queued tasks of array job_id grouped by job state.

    Always passes --only-job-state to print the full task list. Otherwise, %i
    appends the %-throttle suffix and truncates the expression at 64 characters.
    """

    cmd = f"squeue --noheader --only-job-state --format='%i|%T' --jobs={job_id}"
    if states is not None:
        cmd += f" --states={states}"
    kwargs.setdefault("fatal", True)
    output = atf.run_command_output(cmd, **kwargs)

    task_states = {}
    for line in output.split("\n"):
        if "|" in line:
            task_job_id, state = line.split("|")
            _, _, array_spec = task_job_id.partition("_")
            if array_spec:
                task_ids = parse_tasks(array_spec)
                task_states.setdefault(state, set()).update(task_ids)
    return task_states


def get_array_tasks(job_id, **kwargs):
    states = get_array_task_states(job_id, **kwargs)
    return set().union(*states.values())


def resync_running(state):
    """Update the running/pending split from observed state."""
    job_id = state["metajob_id"]
    remaining = set(state["running"]) | state["pending_tasks"]
    # slurmctld offers the lowest remaining task first, so the running set is
    # always the task_limit lowest of whatever is left
    expected_running = set(sorted(remaining)[:task_limit])

    running_tasks = set()
    for _ in atf.timer(fatal=True):
        running_tasks = get_array_tasks(job_id, states="RUNNING", quiet=True)
        if running_tasks == expected_running:
            break
        logging.debug(
            f"Job ({job_id}) has tasks {sorted(running_tasks)} running, "
            f"but we are waiting for {sorted(expected_running)}"
        )

    state["pending_tasks"].clear()
    state["pending_tasks"].update(remaining - running_tasks)
    state["running"].clear()
    state["running"].update(
        {
            task_id: atf.get_job_id_from_array_task(job_id, task_id, fatal=True)
            for task_id in running_tasks
        }
    )

    wait_for_jobs(state["running"].values(), "RUNNING")


def parse_array_task_set(output, job_id, prefix="", suffix=""):
    """Collect the task ids of every ``job_id``_<task> reference in output.

    Both the plain and bracketed forms are handled, since scancel collapses
    consecutive tasks into ``job_id``_[1,4-5].
    """
    task_ids = set()
    # ``.`` is accepted so a truncated expression reaches parse_tasks() and
    # fails there, rather than silently matching a shorter prefix
    array_id = r"(\d+|\[[\d,.\-]+\])"
    # Anchor on the left so job id 123 does not match inside 9123_1
    pattern = prefix + rf"(?<!\d){job_id}_{array_id}" + suffix
    for match in re.finditer(pattern, output):
        task_ids.update(parse_tasks(match.group(1)))
    return task_ids


def parse_error_task_set(output, job_id, reason):
    """Collect the task ids scancel rejected with the given error reason."""
    return parse_array_task_set(
        output, job_id, prefix="Kill job error on job id ", suffix=f": {reason}"
    )


def track_array_cancel(
    state,
    array_task_ids,
    expect_pending=set(),
    expect_running=set(),
    expect_done=set(),
    expect_invalid=set(),
):
    """Cancel the given array tasks and validate scancel's behavior/output.

    The expect_* sets declare which tasks the caller intends to be in each
    state. They are asserted to ensure a change in which tasks the scheduler
    splits fails the test rather than exercising a different (or empty) case.

    Updates the sets and dict in ``state`` in place to reflect the new queue
    (cancelled tasks removed, newly-split tasks moved to running), and returns
    the scancel result.
    """
    job_id = state["metajob_id"]
    pending_tasks = state["pending_tasks"]
    running = state["running"]
    terminated = state["terminated"]

    if len(array_task_ids) == 1:
        expr = str(array_task_ids[0])
    else:
        expr = f"[{atf.list_to_range(array_task_ids)}]"

    result = run_scancel([f"{job_id}_{expr}"])
    output = result["stderr"]

    job_ids_cancelled = []
    cancelled_pending = set()
    cancelled_running = set()
    already_done = set()
    invalid = set()
    for task_id in array_task_ids:
        if task_id in pending_tasks:
            cancelled_pending.add(task_id)
            pending_tasks.remove(task_id)
        elif task_id in running:
            cancelled_running.add(task_id)
            job_ids_cancelled.append(running.pop(task_id))
            terminated.add(task_id)
        elif task_id in terminated:
            already_done.add(task_id)
        else:
            invalid.add(task_id)

    observed = (cancelled_pending, cancelled_running, already_done, invalid)
    expected = (expect_pending, expect_running, expect_done, expect_invalid)
    assert observed == expected, (
        f"tasks {array_task_ids} should be {expected} "
        f"(pending, running, already done, invalid), but were {observed}"
    )

    was_cancelled = cancelled_pending | cancelled_running
    wait_for_jobs(job_ids_cancelled, "CANCELLED")

    reported = parse_array_task_set(output, job_id, prefix="Terminating job ")
    assert (
        reported == was_cancelled
    ), f"scancel should report terminating exactly job {job_id} tasks {was_cancelled}"

    reported = parse_error_task_set(output, job_id, "Invalid job id specified")
    assert (
        reported == invalid
    ), f"scancel should reject exactly job {job_id} tasks {invalid} as invalid"

    reported = parse_error_task_set(
        output, job_id, "Job/step already completing or completed"
    )
    assert (
        reported == already_done
    ), f"scancel should reject exactly job {job_id} tasks {already_done} as already done"

    resync_running(state)
    running_tasks = set(running)

    # Check new job states
    states = get_array_task_states(job_id)
    reported_pending = states.get("PENDING", set())
    assert (
        reported_pending == pending_tasks
    ), f"squeue should report {pending_tasks} pending for job {job_id}"

    reported_running = states.get("RUNNING", set())
    assert (
        reported_running == running_tasks
    ), f"squeue should report {running_tasks} running for job {job_id}"

    assert set(states) <= {
        "PENDING",
        "RUNNING",
    }, f"job {job_id} has tasks in unexpected states: {set(states) - {'PENDING', 'RUNNING'}}"

    return result


def test_big_array():
    """Cancel more than 128 array jobs at once"""

    state = setup_array_state(1000)
    result = track_array_cancel(
        state,
        list(range(1, 500)),
        expect_running=set(range(1, task_limit + 1)),
        expect_pending=set(range(task_limit + 1, 500)),
    )

    # A job-array expression expanding to more than 128 tasks used to segfault
    # scancel. Fixed in 23.11 (Ticket 18464). This is only a crash canary:
    # scancel --ctld exits 0 even when individual tasks fail to cancel.
    assert (
        result["exit_code"] == 0
    ), f"scancel should not crash cancelling 499 array tasks (rc={result['exit_code']})"


def test_array_task_states():
    """Cancel array tasks in different combinations of states"""

    state = setup_array_state(10)

    # Cancel only pending tasks. These have not split from the array yet, and
    # per scancel.1 only the named elements are cancelled, not the whole array.
    track_array_cancel(state, [5, 6, 10], expect_pending={5, 6, 10})

    track_array_cancel(state, [5, 6, 10], expect_invalid={5, 6, 10})

    track_array_cancel(
        state, [4, 5, 6, 10], expect_pending={4}, expect_invalid={5, 6, 10}
    )

    track_array_cancel(
        state, [2, 3, 4, 5], expect_running={2, 3}, expect_invalid={4, 5}
    )

    track_array_cancel(state, [2, 3], expect_done={2, 3})

    # The last pending task splits out, leaving no meta record behind
    track_array_cancel(state, [9, 10], expect_pending={9}, expect_invalid={10})

    track_array_cancel(state, [1, 7, 8], expect_running={1, 7, 8})


def test_array_task_exprs():
    """Cancel with all kinds of array task expressions"""

    state = setup_array_state(10)

    # Cancel running, pending, and non-existent tasks using indices beyond array
    task_ids = [1, 7, 8, 11, 12, 25]
    track_array_cancel(
        state,
        task_ids,
        expect_pending={7, 8},
        expect_running={1},
        expect_invalid={11, 12, 25},
    )

    # Cancel a single running task (no bracket syntax)
    track_array_cancel(state, [2], expect_running={2})

    # Cancel the whole array
    job_id = state["metajob_id"]
    result = run_scancel([job_id])
    wait_for_jobs(state["running"].values(), "CANCELLED")

    assert (
        result["exit_code"] == 0
    ), f"scancel should cancel array {job_id} (rc={result['exit_code']})"

    output = result["stderr"]
    # The still-pending tasks are cancelled through the meta record, which
    # scancel reports without an array id
    assert re.search(
        rf"Terminating job {job_id}(?!\d|_)", output
    ), f"scancel should report terminating the meta record of job {job_id}"

    reported = parse_array_task_set(output, job_id, prefix="Terminating job ")
    assert reported == set(
        state["running"]
    ), f"scancel should report terminating split tasks {set(state['running'])}"

    reported = parse_error_task_set(
        output, job_id, "Job/step already completing or completed"
    )
    assert (
        reported == state["terminated"]
    ), f"scancel should reject already cancelled tasks {state['terminated']}"

    # Sanity check that no tasks of this array remain queued
    tasks = get_array_tasks(job_id)
    assert not tasks, f"squeue should report no jobs for {job_id}"


def submit_running_array(task_ids):
    """Submit an unthrottled array and wait for every task to be running.

    Left unthrottled so that every task splits, which keeps these tests
    independent of which tasks the scheduler would have picked first.
    """
    job_id = submit_job(atf.list_to_range(task_ids), "srun sleep infinity")
    for _ in atf.timer(fatal=True):
        running_tasks = get_array_tasks(job_id, states="RUNNING", quiet=True)
        if running_tasks == set(task_ids):
            break
    return job_id


def test_array_task_index_zero():
    """Cancel the minimum array task index"""

    job_id = submit_running_array(range(task_limit))
    task_job_id = atf.get_job_id_from_array_task(job_id, 0, fatal=True)

    result = run_scancel([f"{job_id}_0"], fatal=True)
    assert parse_array_task_set(
        result["stderr"], prefix="Terminating job ", job_id=job_id
    ) == {0}, f"scancel should report terminating only task 0 of job {job_id}"
    atf.wait_for_job_state(task_job_id, "CANCELLED", fatal=True)

    assert get_array_tasks(job_id) == set(
        range(1, task_limit)
    ), f"tasks {set(range(1, task_limit))} of job {job_id} should survive cancelling task 0"


def test_array_completed_task():
    """Cancel array tasks that ran to completion"""

    job_id = submit_job("1-2", "srun true")
    for _ in atf.timer(fatal=True):
        tasks = get_array_tasks(job_id, quiet=True)
        if not tasks:
            break

    # A completed task still has a job record, so it is rejected as already
    # done rather than as an invalid job id
    result = run_scancel([f"{job_id}_1", f"{job_id}_[2]"])
    reported = parse_error_task_set(
        result["stderr"], job_id, "Job/step already completing or completed"
    )
    assert reported == {
        1,
        2,
    }, f"scancel should reject completed tasks of job {job_id} as already done"

    assert (
        parse_error_task_set(result["stderr"], job_id, "Invalid job id specified")
        == set()
    ), f"scancel should not report completed tasks of job {job_id} as invalid"


@pytest.mark.parametrize("ctld", [True, False])
def test_array_unsplit_tasks(ctld):
    """Cancel array tasks while the whole array is still pending"""

    job_id = submit_job("1-6", "srun sleep infinity", extra="--hold")
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    assert (
        atf.get_job_id_from_array_task(job_id, 2) == 0
    ), f"task 2 of job {job_id} should not have split from the array"

    result = run_scancel([f"{job_id}_[2-3]"], ctld=ctld, fatal=True)
    assert parse_array_task_set(
        result["stderr"], job_id, prefix="Terminating job "
    ) == {2, 3}, f"scancel should report terminating only tasks 2-3 of job {job_id}"

    assert get_array_tasks(job_id, states="PENDING") == {
        1,
        4,
        5,
        6,
    }, f"cancelling unsplit tasks of job {job_id} should leave the rest queued"

    result = run_scancel([f"{job_id}_5"], ctld=ctld, fatal=True)
    assert parse_array_task_set(
        result["stderr"], job_id, prefix="Terminating job "
    ) == {5}, f"scancel should report terminating only task 5 of job {job_id}"

    assert get_array_tasks(job_id, states="PENDING") == {
        1,
        4,
        6,
    }, f"cancelling unsplit task {job_id}_5 should leave the rest queued"


@pytest.mark.parametrize("ctld", [True, False])
def test_array_split_pending_tasks(ctld):
    """Cancel pending array tasks one at a time after they split from the array"""

    task_ids = {1, 2, 3}
    job_id = submit_job(
        atf.list_to_range(sorted(task_ids)), "srun sleep infinity", extra="--hold"
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    # Updating a pending task splits it from the array
    for task_id in task_ids:
        atf.run_command(
            f"scontrol update jobid={job_id}_{task_id} timelimit=5",
            fatal=True,
        )
        assert (
            atf.get_job_id_from_array_task(job_id, task_id) != 0
        ), f"task {task_id} of job {job_id} should have split from the array"

    for task_id in sorted(task_ids, reverse=True):
        result = run_scancel([f"{job_id}_{task_id}"], ctld=ctld, fatal=True)
        assert parse_array_task_set(
            result["stderr"], job_id, prefix="Terminating job "
        ) == {task_id}, f"scancel should report terminating only {job_id}_{task_id}"

        task_ids.remove(task_id)
        assert (
            get_array_tasks(job_id, states="PENDING") == task_ids
        ), f"cancelling split task {job_id}_{task_id} should leave {task_ids} queued"


def test_array_signal_tasks():
    """Signal a subset of array tasks with the bracketed form"""

    task_ids = range(1, task_limit + 1)
    job_id = submit_running_array(task_ids)
    signaled = [1, task_limit]
    expr = f"[{atf.list_to_range(signaled)}]"

    task_jobs = {}
    for task_id in task_ids:
        task_job_id = atf.get_job_id_from_array_task(job_id, task_id, fatal=True)
        atf.wait_for_step(task_job_id, 0, fatal=True)
        task_jobs[task_id] = task_job_id

    run_scancel([f"{job_id}_{expr}"], signal="SIGSTOP", fatal=True)
    for task_id, task_job_id in task_jobs.items():
        expected = "STOPPED" if task_id in signaled else "RUNNING"
        atf.wait_for_job_state(task_job_id, expected, fatal=True)

    run_scancel([f"{job_id}_{expr}"], signal="SIGCONT", fatal=True)
    wait_for_jobs(task_jobs.values(), "RUNNING")

    run_scancel([job_id], fatal=True)
    wait_for_jobs(task_jobs.values(), "CANCELLED")
