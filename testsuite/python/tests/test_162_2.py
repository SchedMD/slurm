############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
#
# Verify swait's steps-drained push path (REQUEST_STEPS_DRAINED_SUBSCRIBE
# subscribe + SRUN_STEPS_DRAINED push from stepmgr).
############################################################################
import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        reason="swait push notifications were added in 26.05",
    )
    atf.require_version(
        (26, 5),
        component="sbin/slurmd",
        reason="REQUEST_STEPS_DRAINED_SUBSCRIBE relay lives in slurmd 26.05",
    )
    atf.require_tool("swait")
    atf.require_version(
        (26, 5),
        component="bin/swait",
        reason="swait push client was added in 26.05",
    )
    atf.require_nodes(1)
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


# Long enough that swait subscribes while the step is still running.
LATENCY_SLEEP_SECS = 6
# Catches latency regressions in the SRUN_STEPS_DRAINED wake path.
LATENCY_CEILING_SECS = LATENCY_SLEEP_SECS + 8
# Catches instant-return regressions and partial-wake regressions where
# swait wakes on an intermediate event before all steps have drained.
# Margin tolerates swait startup + subscribe handshake on loaded runners.
LATENCY_FLOOR_SECS = LATENCY_SLEEP_SECS - 3


def _resolve_array_task_id(master_id, task_offset, timeout=60):
    """Poll until the per-task job id for master_id_task_offset is
    observable in squeue. See test_162_1 for the rationale.
    """

    state = {"id": 0}

    def _try():
        jobs = atf.get_jobs(quiet=True)
        for raw_id, job in jobs.items():
            if (
                job.get("ArrayJobId") == master_id
                and job.get("ArrayTaskId") == task_offset
            ):
                state["id"] = raw_id
                return raw_id
        return 0

    if not atf.repeat_until(_try, lambda x: x != 0, timeout=timeout):
        pytest.fail(f"Array task {master_id}_{task_offset} never appeared in squeue")
    return state["id"]


def _swait_push(swait_args, sleep_secs, time_limit="5:00", xfail=False):
    """Submit a job with one user step, then run swait on the env fast path.

    Returns (exit_code, elapsed_seconds). Setting SLURM_STEPMGR forces
    swait to take the push/fast path instead of the ctld discovery
    path; for a 1-node stepmgr job the stepmgr is the batch host.

    IN xfail - True for callers that expect swait to exit nonzero
        (e.g., --timeout firing).
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 --time={time_limit} --job-name=test_162_2 "
        f"--wrap 'srun -n1 sleep {sleep_secs}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=60, fatal=True)
    stepmgr = atf.get_job_parameter(job_id, "BatchHost")
    result = atf.run_command(
        f"swait {swait_args} {job_id}",
        env_vars=f"SLURM_STEPMGR={stepmgr}",
        timeout=180,
        xfail=xfail,
    )
    return result["exit_code"], result["duration"]


def test_push_latency_user_step():
    """swait exits within a few seconds of the user step ending."""

    rc, elapsed = _swait_push("", LATENCY_SLEEP_SECS)
    assert rc == 0, f"swait exited {rc}, expected 0"
    assert (
        elapsed >= LATENCY_FLOOR_SECS
    ), f"swait returned in {elapsed:.1f}s; expected to wait for step end"
    assert (
        elapsed < LATENCY_CEILING_SECS
    ), f"swait took {elapsed:.1f}s; push path appears broken"


def test_timeout_granularity_short():
    """--timeout fires at the user's deadline within conmgr's timer granularity."""

    TIMEOUT_SECS = 5
    rc, elapsed = _swait_push(f"--timeout {TIMEOUT_SECS}", sleep_secs=120, xfail=True)
    assert rc == 1, f"swait exited {rc}, expected 1 for --timeout"
    # Ceiling absorbs swait startup + conmgr init + subscribe RPC +
    # the timer + shutdown on loaded runners; match LATENCY_CEILING_SECS
    # budget (+8) to be consistent with other timing-bound tests.
    assert TIMEOUT_SECS - 1 <= elapsed < TIMEOUT_SECS + 8, (
        f"swait --timeout={TIMEOUT_SECS} took {elapsed:.1f}s; expected "
        "deadline-granularity exit"
    )


def test_already_drained_returns_zero():
    """Subscribing to a job with no regular steps (only the batch
    step) returns ESLURM_STEPS_DRAINED; swait fast-returns 0.
    """

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_2_drained --wrap 'sleep 15'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", timeout=60, fatal=True)
    stepmgr = atf.get_job_parameter(job_id, "BatchHost")
    result = atf.run_command(
        f"swait {job_id}",
        env_vars=f"SLURM_STEPMGR={stepmgr}",
        timeout=30,
    )
    assert (
        result["exit_code"] == 0
    ), f"swait exited {result['exit_code']}; stderr: {result['stderr']!r}"
    assert (
        result["duration"] < 10
    ), f"swait took {result['duration']:.1f}s; expected immediate fast-return"


def test_pending_async_step_blocks_swait():
    """A pending async step keeps swait blocked until that step also
    drains. Per the design, only running steps and pending async steps
    block swait; pending regular placeholders do not (see
    test_queued_regular_step_does_not_block).

    A regular srun holds the 1 CPU; an async srun is submitted while
    the regular step runs and ends up pending in stepmgr. swait should
    wait through both steps, exiting only after the async step drains.
    """

    REG_SECS = 3
    ASYNC_SECS = 4
    # Trailing sleep keeps the batch script alive past the async step's
    # finish so the async record drains naturally instead of being
    # signal-killed during job teardown.
    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_2_async_pending "
        f"--wrap 'srun -n1 sleep {REG_SECS} & sleep 0.5; "
        f"srun --async -n1 sleep {ASYNC_SECS}; wait; "
        f"sleep {ASYNC_SECS + 2}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=60, fatal=True)
    stepmgr = atf.get_job_parameter(job_id, "BatchHost")
    result = atf.run_command(
        f"swait {job_id}",
        env_vars=f"SLURM_STEPMGR={stepmgr}",
        timeout=REG_SECS + ASYNC_SECS + 30,
    )
    assert (
        result["exit_code"] == 0
    ), f"swait exited {result['exit_code']}; stderr: {result['stderr']!r}"
    # Floor must stay much smaller than ASYNC_SECS so a swait that wakes
    # on regular-step end (missing the pending async) doesn't slip past.
    assert result["duration"] >= REG_SECS + ASYNC_SECS - 1, (
        f"swait returned in {result['duration']:.1f}s; expected "
        f">= {REG_SECS + ASYNC_SECS}s (pending async should keep swait blocked)"
    )


def test_queued_regular_step_does_not_block():
    """Two sequential regular sruns on a 1-CPU node, no --overlap. The
    second srun queues behind the first as a pending placeholder. When
    the first step drains, swait returns; the pending regular
    placeholder must not keep swait blocked.

    Per the design, pending regular sruns are bystanders to swait;
    only running steps and pending async steps block it.
    """

    STEP_SECS = 5
    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_2_reg_queue "
        f"--wrap 'srun -n1 sleep {STEP_SECS} & "
        f"srun -n1 sleep {STEP_SECS} & wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=60, fatal=True)
    stepmgr = atf.get_job_parameter(job_id, "BatchHost")
    result = atf.run_command(
        f"swait {job_id}",
        env_vars=f"SLURM_STEPMGR={stepmgr}",
        timeout=STEP_SECS * 2 + 30,
    )
    assert (
        result["exit_code"] == 0
    ), f"swait exited {result['exit_code']}; stderr: {result['stderr']!r}"
    # Floor must remain a meaningful fraction of STEP_SECS so an
    # instant-return regression (e.g., swait fast-returning while the
    # queued placeholder is in the list) is caught.
    assert STEP_SECS - 1 <= result["duration"] < STEP_SECS + 3, (
        f"swait took {result['duration']:.1f}s; expected ~{STEP_SECS}s "
        "(queued regular step must not block swait)"
    )


def test_push_via_sluid_env():
    """swait s<sluid> with SLURM_JOB_SLUID + SLURM_STEPMGR set takes
    the SLUID branch of _resolve_stepmgr (sibling to the numeric jobid
    fast path in test_push_latency_user_step).
    """

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_2_sluid "
        f"--wrap 'srun -n1 sleep {LATENCY_SLEEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=60, fatal=True)
    sluid = atf.get_job_parameter(job_id, "SLUID")
    stepmgr = atf.get_job_parameter(job_id, "BatchHost")
    result = atf.run_command(
        f"swait {sluid}",
        env_vars=f"SLURM_JOB_SLUID={sluid} SLURM_STEPMGR={stepmgr}",
        timeout=180,
    )
    assert (
        result["exit_code"] == 0
    ), f"swait exited {result['exit_code']}; stderr: {result['stderr']!r}"
    elapsed = result["duration"]
    assert (
        elapsed >= LATENCY_FLOOR_SECS
    ), f"swait returned in {elapsed:.1f}s; expected to wait for step end"
    assert (
        elapsed < LATENCY_CEILING_SECS
    ), f"swait took {elapsed:.1f}s; push path appears broken"


def test_array_task_env_fast_path():
    """Array task with SLURM_ARRAY_JOB_ID/TASK_ID + SLURM_JOB_ID +
    SLURM_STEPMGR set takes the _array_task_env_stepmgr fast path.
    """

    master_id = atf.submit_job_sbatch(
        "--array=0-1 -N1 --time=5:00 --job-name=test_162_2_array "
        f"--wrap 'srun -n1 sleep {LATENCY_SLEEP_SECS}'",
        fatal=True,
    )
    task0_id = _resolve_array_task_id(master_id, 0)
    atf.wait_for_step(f"{master_id}_0", 0, timeout=60, fatal=True)
    stepmgr = atf.get_job_parameter(task0_id, "BatchHost")
    result = atf.run_command(
        f"swait {master_id}_0",
        env_vars=(
            f"SLURM_ARRAY_JOB_ID={master_id} SLURM_ARRAY_TASK_ID=0 "
            f"SLURM_JOB_ID={task0_id} SLURM_STEPMGR={stepmgr}"
        ),
        timeout=180,
    )
    assert (
        result["exit_code"] == 0
    ), f"swait exited {result['exit_code']}; stderr: {result['stderr']!r}"
    assert (
        result["duration"] >= LATENCY_FLOOR_SECS
    ), f"swait returned in {result['duration']:.1f}s; expected to wait"


def test_unreachable_stepmgr_exits_two():
    """swait exits 2 when the stepmgr host cannot be resolved/reached.

    Exercises the _setup_push failure path: an unresolvable
    SLURM_STEPMGR forces slurm_send_recv_node_msg() to fail before any
    subscribe RPC is even attempted. Exit 1 is reserved for --timeout;
    any other runtime/network failure exits 2.
    """

    # Below MAX_VAL (0xfffffff0) so swait's env fast-path is not
    # rejected by the parsed-jobid sanity check.
    BOGUS_JOBID = 4000000000
    BOGUS_HOST = "no-such-host.invalid"
    result = atf.run_command(
        f"swait {BOGUS_JOBID}",
        env_vars=f"SLURM_JOB_ID={BOGUS_JOBID} SLURM_STEPMGR={BOGUS_HOST}",
        xfail=True,
        timeout=30,
    )
    assert (
        result["exit_code"] == 2
    ), f"swait exited {result['exit_code']}, expected 2 (stderr: {result['stderr']!r})"
    assert (
        "subscribe to stepmgr" in result["stderr"]
    ), f"unexpected stderr: {result['stderr']!r}"
