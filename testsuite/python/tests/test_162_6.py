############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify what swait reports for a step that ends in a terminal-failure state.

Covers the signal path (code=<status>:<signal> with no reason), the
reason=TIMEOUT decode and its raw --json state, and waits on a still-pending
async step that either launches and completes, is cancelled before launch
(rendering "never-launched"), or is caught by a whole-job cancel.

Terminal states are asserted in both the human and the --json rendering
here, because the state decode itself is what is under test.
"""

import json
import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Issue 50928: per-step SRUN_STEPS_DRAINED dispatch lives in slurmd 26.11",
    )
    atf.require_tool("swait")
    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: swait --follow/--json were added in 26.11",
    )
    # 4 CPUs: the pending-step tests park a -n4 blocker on the node so the
    # async step they subscribe to stays PENDING. The 2 nodes are the shape the
    # whole test_162_5..8 family requires, kept identical here so the suite
    # does not reconfigure between them.
    atf.require_nodes(2, [("CPUs", 4)])
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


# run_command timeout for a swait that blocks on a step: the step's own
# lifetime plus job startup and the subscribe round trip, with headroom for
# a loaded runner. Raised over atf's 60s default so an outer kill cannot
# mask a real wait.
BLOCKING_CMD_SECS = 180

# Raised over atf's 45s default: these modules park several concurrent
# steps, so the first step can take longer than that to appear.
STEP_WAIT_SECS = 60


@pytest.mark.parametrize("fmt", ["", "--json"])
def test_step_signal_emits_no_spurious_reason(fmt):
    """A signal-killed step is still RUNNING at dispatch, so swait must
    convey the kill only via code=<status>:<signal> and emit no reason
    (reasons are for terminal-failure states only)."""

    out = "sig.out"
    err = "sig.err"
    # Gate the scancel on the step actually RUNNING and on swait having
    # subscribed: killing a still-PENDING step would instead render
    # never-launched reason=CANCELLED and fail both assertions below.
    # -v goes to stderr, so stdout stays clean for the --json arm.
    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_6_sig "
        "--wrap '"
        "srun --async -n1 sleep infinity; "
        "for _ in $(seq 300); do "
        "scontrol show step $SLURM_JOB_ID.0 2>/dev/null | grep -q State=RUNNING "
        "&& break; sleep 0.2; done; "
        f"swait -v {fmt} $SLURM_JOB_ID.0 > {out} 2>{err} & SW=$!; "
        + atf.swait_await_subscribe(err, quote='"')
        + "scancel $SLURM_JOB_ID.0; "
        "wait $SW; exit $?'",
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "COMPLETED", timeout=120, fatal=False), (
        "job did not complete; swait did not report the step after cancel"
        f"; a job ExitCode of {atf.SWAIT_SUBSCRIBE_TIMEOUT_RC}:0 means the subscribe "
        f"sentinel {atf.SWAIT_SUBSCRIBED!r} never appeared in swait -v output, which is "
        "a harness/log-text problem rather than a notification failure"
    )
    # swait wrote this on the compute node, so job completion does not imply
    # the file is visible here yet. Wait on a marker that only appears once
    # the record is complete, so the read below cannot catch a partial line.
    # For --json that is "state", the last field swait emits. For the human
    # rendering "code=" is not enough -- it precedes both the value and any
    # trailing reason=, and the negative reason= assertion below would pass
    # on a truncated line -- so poll for the full code=<status>:<signal>.
    if fmt:
        atf.assert_file_contents(out, '"state"', contains=True)
    else:
        pattern = re.compile(rf"StepId={job_id}\.0 code=\d+:\d+")
        for _ in atf.timer(timeout=60, poll_interval=1, quiet=True):
            if pattern.search(atf.run_command_output(f"cat {out}", quiet=True)):
                break
        else:
            pytest.fail(f"no complete completion line in {out}")
    output = atf.run_command_output(f"cat {out}", fatal=True)

    if not fmt:
        assert re.search(
            rf"StepId={job_id}\.0 code=0:[1-9]", output
        ), f"expected a non-zero kill signal in the exit code; got: {output!r}"
        assert (
            "reason=" not in output
        ), f"a RUNNING (signal-killed) step must not emit a reason; got: {output!r}"
        return

    objs = [json.loads(line) for line in output.splitlines() if line.strip()]
    assert objs, f"no JSON completion emitted: {output!r}"
    obj = objs[0]
    # swait.1: exit_code.status names the outcome; SIGNALED is the value
    # documented for a step a signal killed.
    status = obj["exit_code"]["status"]
    assert "SIGNALED" in (
        status if isinstance(status, list) else [status]
    ), f"expected SIGNALED in exit_code.status: {obj!r}"
    # swait.1: exit_code carries signal, an object with id and name.
    signal = obj["exit_code"]["signal"]
    assert atf.get_data_parser_number(signal["id"]) not in (
        None,
        0,
    ), f"expected a non-zero signal id for a killed step: {obj!r}"
    assert signal["name"], f"expected a signal name: {obj!r}"
    assert atf.get_data_parser_number(obj["exit_code"]["return_code"]) in (
        None,
        0,
    ), f"a signal-killed step reports the signal, not a return code: {obj!r}"


@pytest.mark.slow
@pytest.mark.parametrize("fmt", ["", "--json"])
def test_step_timeout_reason_decoded(fmt):
    """A step that exceeds its time limit reaches TIMEOUT state, which swait
    decodes client-side (from the raw state on the wire). The human
    rendering names it as reason=TIMEOUT; --json reports the raw terminal
    state, not clamped to COMPLETED the way a normal completion is."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=10:00 --job-name=test_162_6_timeout "
        "--wrap 'srun --async -n1 --time=1 sleep infinity; sleep infinity'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    # The step's --time=1 (one minute) elapses, stepmgr sets it TIMEOUT and
    # kills it; swait is notified of the step's end.
    result = atf.run_command(
        f"swait {fmt} {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"

    if not fmt:
        assert "reason=TIMEOUT" in result["stdout"], (
            "expected reason=TIMEOUT decoded from step state; got: "
            f"{result['stdout']!r}"
        )
        return

    obj = json.loads(result["stdout"].strip())
    assert (
        atf.get_data_parser_flag(obj["state"]) == "TIMEOUT"
    ), f"bad json: {result['stdout']!r}"


@pytest.mark.slow
def test_step_wait_pending_then_completes():
    """A swait on a still-PENDING async step must follow it into its
    launched record and report the real completion, not time out."""

    # The blocker must outlast the whole setup path (wait_for_step's 1s
    # poll, atf.get_pending_async_step()'s poll over get_steps, and _stepmgr's
    # scontrol round trip), or the async step launches before swait
    # subscribes and the pending-subscriber path is never exercised.
    BLOCK = 60
    job_id = atf.submit_job_sbatch(
        "-N1 -n4 --time=5:00 --job-name=test_162_6_pending_done "
        f"--wrap 'srun -n4 sleep {BLOCK} & sleep 1; "
        # Trailing sleep keeps the job alive well past the blocker so the
        # async step has room to launch and end under CI load; swait exits
        # on the per-step push, so this costs the test no wall time.
        "srun --async -n1 sleep 3; wait; sleep 30'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    pend = atf.get_pending_async_step(job_id)
    assert pend is not None, "expected a pending async step to subscribe to"

    result = atf.run_command(
        f"swait --json --timeout={BLOCK + 90} {pend}",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCK + 120,
    )
    assert result["exit_code"] == 0, (
        f"swait exited {result['exit_code']}; the pending step's completion "
        f"was not delivered. stderr: {result['stderr']!r}"
    )
    # A fast return prints nothing, so name that case rather than letting
    # json.loads raise an opaque decode error on an empty string.
    assert result["stdout"].strip(), (
        "swait returned no completion; the async step had already ended when "
        "swait subscribed, so the pending-subscriber path was not exercised"
    )
    obj = json.loads(result["stdout"].strip())
    # The async step launches once the blocker frees CPUs; depending on timing
    # it may exit cleanly or be signaled when the job ends. The regression
    # guarded here is the orphaned subscriber (swait timing out), so assert
    # swait was notified of the target step's terminal end -- not a specific
    # exit code.
    assert (
        obj["step_id"]["step_id"] == pend.split(".")[-1]
    ), f"bad json: {result['stdout']!r}"
    assert atf.get_data_parser_flag(obj["exit_code"]["status"]) in (
        "SUCCESS",
        "ERROR",
        "SIGNALED",
        "CORE_DUMPED",
    ), f"step did not launch and end: {result['stdout']!r}"


@pytest.mark.parametrize("fmt", ["", "--json"])
def test_step_wait_pending_cancelled(fmt):
    """A swait on a still-PENDING async step must wake (never-launched)
    when that step is cancelled before launch, not hang to --timeout."""

    # The blocker holds the CPUs until teardown, so the async step cannot
    # launch out from under the wait; scancel is the only terminator.
    job_id = atf.submit_job_sbatch(
        "-N1 -n4 --time=5:00 --job-name=test_162_6_pending_cancel "
        "--wrap 'srun -n4 sleep infinity & sleep 1; "
        "srun --async -n1 sleep 5; wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    pend = atf.get_pending_async_step(job_id)
    assert pend is not None, "expected a pending async step to subscribe to"

    # Split the streams: -v writes the subscribe sentinel to stderr, so
    # keeping stdout clean lets the --json arm parse it directly.
    out_file = "pending_cancel.err"
    stdout_file = "pending_cancel.out"
    # Background swait (subscribe to the pending step), then scancel that
    # step while it is still pending; `wait` makes the script's rc swait's.
    script = (
        f"env SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)} "
        f"swait -v {fmt} --timeout=30 {pend} "
        f">{stdout_file} 2>{out_file} & sw=$!; "
        + atf.swait_await_subscribe(out_file)
        + f"scancel {pend}; wait $sw"
    )
    result = atf.run_command(script, timeout=BLOCKING_CMD_SECS)
    assert result["exit_code"] == 0, (
        "swait was not notified of the pending step's cancellation "
        f"(exited {result['exit_code']})"
        f"; {atf.SWAIT_SUBSCRIBE_TIMEOUT_RC} means the subscribe sentinel "
        f"{atf.SWAIT_SUBSCRIBED!r} never appeared in swait -v output, which is a "
        f"harness/log-text problem rather than a notification failure"
    )
    out = atf.run_command_output(f"cat {stdout_file}", fatal=True)

    if not fmt:
        # swait.1: a never-launched step always carries a reason=, normally
        # FAILED or CANCELLED. Assert the whole documented line, not tokens.
        assert re.search(
            rf"StepId={re.escape(pend)} never-launched reason=(FAILED|CANCELLED)\b",
            out,
        ), f"expected the documented never-launched line: {out!r}"
        return

    objs = [json.loads(line) for line in out.splitlines() if line.strip()]
    assert objs, f"no JSON completion emitted: {out!r}"
    obj = objs[0]
    # swait.1: a never-launched step has PENDING in status and an unset
    # return_code, "which is what distinguishes it from a step that exited 0".
    status = obj["exit_code"]["status"]
    status = status if isinstance(status, list) else [status]
    assert "PENDING" in status, f"expected PENDING in exit_code.status: {obj!r}"
    assert (
        atf.get_data_parser_number(obj["exit_code"]["return_code"]) is None
    ), f"a never-launched step must not carry a return_code: {obj!r}"
    assert atf.get_data_parser_flag(obj["state"]) in (
        "CANCELLED",
        "FAILED",
    ), f"expected a terminal failure state: {obj!r}"


def _pending_jobcancel_output():
    """Runs a STEP wait on a pending async step, cancels the whole job, and
    returns what swait printed. Fails if swait did not wake at all."""

    job_id = atf.submit_job_sbatch(
        "-N1 -n4 --time=5:00 --job-name=test_162_6_pending_jobcancel "
        "--wrap 'srun -n4 sleep infinity & sleep 1; "
        "srun --async -n1 sleep 5; wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    pend = atf.get_pending_async_step(job_id)
    assert pend is not None, "expected a pending async step to subscribe to"

    out_file = "pending_jobcancel.out"
    # Background swait (subscribe to the pending step), then scancel the
    # *whole job* -- not the step -- while it is still pending.
    script = (
        f"env SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)} "
        f"swait -v --timeout=30 {pend} >{out_file} 2>&1 & sw=$!; "
        + atf.swait_await_subscribe(out_file)
        + f"scancel {job_id}; wait $sw"
    )
    result = atf.run_command(script, timeout=BLOCKING_CMD_SECS)
    assert result["exit_code"] == 0, (
        "swait was not notified when the job was cancelled "
        f"(exited {result['exit_code']})"
        f"; {atf.SWAIT_SUBSCRIBE_TIMEOUT_RC} means the subscribe sentinel "
        f"{atf.SWAIT_SUBSCRIBED!r} never appeared in swait -v output, which is a "
        f"harness/log-text problem rather than a notification failure"
    )
    out = atf.run_command_output(f"cat {out_file}", fatal=True)
    # swait.1 permits either shape here: a jobid.stepid wait "can also be
    # satisfied by the whole-set drain".
    per_step_never_launched = (pend in out) and ("never-launched" in out)
    whole_set_drained = f"JobId={job_id} steps drained" in out
    assert (
        per_step_never_launched or whole_set_drained
    ), f"swait did not wake for the pending step wait: {out!r}"
    return pend, out


def test_step_wait_pending_no_hang_on_job_cancel():
    """A STEP subscriber does not hang when the whole job (not the step) is
    cancelled while its target step is still PENDING. Either documented
    shape satisfies the wait; this pins only that swait wakes."""

    _pending_jobcancel_output()


def test_step_wait_pending_reports_never_launched_on_job_cancel():
    """The per-step never-launched line is the more useful of the two shapes
    swait.1 permits, and is what a cancelled pending step now reports:
    _delete_pending_steps() forces the step terminal before the push, so the
    subscriber gets the per-step result rather than the whole-set drain."""

    pend, out = _pending_jobcancel_output()
    assert pend in out and "never-launched" in out, (
        "expected the per-step never-launched line rather than the "
        f"whole-set drain: {out!r}"
    )


def test_step_wait_json_reports_target_on_job_cancel():
    """A jobid.stepid wait satisfied by the whole-set drain emits no object
    at all (the silent exit-3 path): swait never emits the "entire"
    terminator in STEP mode, so a --json consumer of a per-step wait only
    ever sees the target step's own result. Pin that shape here.
    """

    job_id = atf.submit_job_sbatch(
        "-N1 -n4 --time=5:00 --job-name=test_162_6_json_drain "
        "--wrap 'srun -n4 sleep infinity & sleep 1; "
        "srun --async -n1 sleep 5; wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    pend = atf.get_pending_async_step(job_id)
    assert pend is not None, "expected a pending async step to subscribe to"

    out_file = "json_drain.out"
    err_file = "json_drain.err"
    # -v writes the subscribe sentinel to stderr, so stdout stays parseable.
    # Cancel the whole job, which is the case that reaches the terminator.
    script = (
        f"env SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)} "
        f"swait -v --json --timeout=30 {pend} "
        f">{out_file} 2>{err_file} & sw=$!; "
        + atf.swait_await_subscribe(err_file)
        + f"scancel {job_id}; wait $sw"
    )
    result = atf.run_command(script, timeout=BLOCKING_CMD_SECS)
    assert result["exit_code"] == 0, (
        "swait was not notified when the job was cancelled "
        f"(exited {result['exit_code']})"
        f"; {atf.SWAIT_SUBSCRIBE_TIMEOUT_RC} means the subscribe sentinel "
        f"{atf.SWAIT_SUBSCRIBED!r} never appeared in swait -v output, which is a "
        f"harness/log-text problem rather than a notification failure"
    )
    out = atf.run_command_output(f"cat {out_file}", fatal=True)
    objs = [json.loads(line) for line in out.splitlines() if line.strip()]
    assert objs, f"no JSON completion emitted: {out!r}"
    obj = objs[0]

    # The target step's own result satisfied the wait.
    assert (
        obj["step_id"]["step_id"] == pend.split(".")[-1]
    ), f"expected the target step's result: {obj!r}"
    assert "exit_code" in obj, f"a step result must carry an exit_code: {obj!r}"
    assert "state" in obj, f"a step result must carry a state: {obj!r}"


def test_step_wait_reports_target_on_job_cancel():
    """A STEP subscriber's target step is RUNNING when the whole job is
    force-killed: every teardown path funnels through
    _internal_step_complete(), so the subscriber gets the real per-step
    push, not just the generic drain terminator."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_6_running_jobcancel "
        "--wrap 'srun --async -n1 sleep infinity; sleep infinity'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)

    out_file = "running_jobcancel.out"
    script = (
        f"env SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)} "
        f"swait -v --timeout=30 {job_id}.0 >{out_file} 2>&1 & sw=$!; "
        + atf.swait_await_subscribe(out_file)
        + f"scancel {job_id}; wait $sw"
    )
    result = atf.run_command(script, timeout=BLOCKING_CMD_SECS)
    assert result["exit_code"] == 0, (
        "swait was not notified of the force-killed step "
        f"(exited {result['exit_code']})"
        f"; {atf.SWAIT_SUBSCRIBE_TIMEOUT_RC} means the subscribe sentinel "
        f"{atf.SWAIT_SUBSCRIBED!r} never appeared in swait -v output, which is a "
        f"harness/log-text problem rather than a notification failure"
    )
    out = atf.run_command_output(f"cat {out_file}", fatal=True)
    assert (
        f"StepId={job_id}.0" in out
    ), f"expected the real per-step line, not just the drain line: {out!r}"
    assert "code=" in out, f"missing exit code in step line; got: {out!r}"
    assert (
        f"JobId={job_id} steps drained" not in out
    ), f"did not expect the generic drain line: {out!r}"
