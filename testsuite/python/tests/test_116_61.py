############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a pending synchronous step is assigned a real step ID at submission.

Issue 50938: Convert synchronous steps to receive a StepId at submit (like asynchronous
steps) instead of at launch. Visible with `scontrol show step` while still
queued (returns a real stepid, not TBD), launches with the same StepId, and
can be cancelled by StepId before it launches.

The feature is gated on the create request's negotiated protocol_version, so
these tests are skipped unless srun is 26.11+. What an older srun does
instead is deliberately not asserted here: it varies by release rather than
being one "pre-26.11" behavior, so pinning it would only encode whichever
older client the run happened to use.

The exception is test_het_step_component_queues_instead_of_failing, which
observes State=PENDING rather than a StepId and so holds on both sides.
"""

import re
from pathlib import Path

import pytest

import atf

# srun's pending-step wait floor is MAX(60, SlurmctldTimeout); setup() pins
# SlurmctldTimeout to this so the retry tests' wait stays at that 60s floor.
SLURMCTLD_TIMEOUT = 60

# A pending synchronous step is given a real id only when both srun and the
# daemon serving the step create are 26.11+ (older peers keep the TBD path).
# That daemon is slurmctld, or the batch host's stepmgr under enable_stepmgr;
# either way daemons are always >= every client command, so gating on srun
# already implies the serving side is new enough too.
ID_AT_SUBMIT = atf.get_version("bin/srun") >= (26, 11)

# Tests that can only assert the new behavior.
requires_id_at_submit = pytest.mark.skipif(
    not ID_AT_SUBMIT,
    reason="Issue 50938: pending-step id-at-submit requires a 26.11+ srun",
)

# Every test here queues a step behind a hog and polls for a state change, so
# the module runs for minutes rather than seconds.
pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # Two nodes (each 2 CPUs): the single-node tests use one; the out-of-order
    # test needs a second, free node to launch a later step ahead of a pending.
    atf.require_nodes(2, [("CPUs", 2)])
    # Any value at or below the floor gives the same MAX(60, ...) wait, so
    # accept those rather than reconfiguring a site that already satisfies us.
    atf.require_config_parameter("SlurmctldTimeout", SLURMCTLD_TIMEOUT, "<=")
    atf.require_slurm_running()


def _read_when_contains(path, needle):
    """Return path's contents once needle appears in them.

    wait_for_file() only proves the file exists; srun's final lines can still
    be in flight, so poll the contents instead of reading once."""
    atf.wait_for_file(path, fatal=True)
    text = ""
    for _ in atf.timer():
        text = atf.run_command_output(f"cat {path}", quiet=True, fatal=True)
        if needle in text:
            return text
    assert False, f"expected {needle!r} in {path}, got: {text}"


def _steps_by_state(job_id):
    """Return {state: [step_id, ...]} for job_id's numeric steps via
    atf.get_steps(), which renders pending steps with a real StepId (unlike
    squeue).

    The batch and extern steps are dropped: the batch step is RUNNING for the
    whole job, so an unfiltered RUNNING count is always at least 1 and cannot
    be used to wait for a step to launch.
    """
    result = {}
    for sid, info in atf.get_steps(job_id, quiet=True).items():
        if not re.fullmatch(rf"{job_id}\.\d+", sid):
            continue
        result.setdefault(info["State"], []).append(sid)
    return result


def _step_num(step_id):
    """The numeric step number from a '<jobid>.<n>' StepId string."""
    return int(step_id.rsplit(".", 1)[1])


def _hog_ready_snippet(ready, srun_args="--exclusive -n2"):
    """Bash lines that launch a CPU-hogging background srun and block until it
    actually holds the resources (signalled by touching ready), so a
    following srun sees a genuinely busy allocation instead of racing srun's
    own startup latency.

    Aborts the script if the hog never took the CPUs: the busy allocation is
    a precondition, so a test that ran anyway would report the missing
    contention as a product failure."""
    return (
        f"srun {srun_args} sh -c 'touch \"{ready}\"; exec sleep infinity' &\n"
        f"for _ in $(seq 1 60); do [ -f '{ready}' ] && break; sleep 0.5; done\n"
        f"[ -f '{ready}' ] || {{ echo HOG_NOT_READY; exit 1; }}\n"
    )


def _step_registered_snippet(step_suffix):
    """Bash lines that block until step $SLURM_JOB_ID.<step_suffix> is visible
    to scontrol, so a following srun's submission order isn't racing the
    previous srun's own startup latency (config load, auth, controller RTT).

    Aborts the script if the step never registered, for the same reason as
    _hog_ready_snippet()."""
    show = f"scontrol -o show step $SLURM_JOB_ID.{step_suffix} >/dev/null 2>&1"
    return (
        f"for _ in $(seq 1 60); do {show} && break; sleep 0.5; done\n"
        f"{show} || {{ echo STEP_{step_suffix}_NOT_REGISTERED; exit 1; }}\n"
    )


def _submit_hog_and_pending():
    """Allocate a 2-CPU node, hog both CPUs with step .0, and queue step .1
    which must wait for the step resources. Returns the job id. Neither step
    ends on its own, so the test alone controls when resources free."""
    script = Path("hog_and_pending.sh")
    ready = Path("hog_ready")
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready)
        + "srun --exclusive -n2 sleep infinity &\n"
        + "wait\n",
    )
    job_id = atf.submit_job(
        "sbatch", "-N1 -n2 -t5", str(script), wrap_job=False, fatal=True
    )
    assert job_id != 0, "sbatch should submit the job"

    # The hog is always step .0 (first submitted); it must be running before
    # the second step (.1) can pend on it.
    atf.wait_for_step(job_id, 0, fatal=True)
    return job_id


def _submit_wide_hog_and_pending():
    """Allocate 2 nodes (4 CPUs), hog all of them with step .0, and queue a
    1-task step .1 asking 2 CPUs on one node. The task count (1), the CPUs the
    step would hold (2) and the job's CPU count (4) are all distinct, so an
    assertion on the queued step's fields can only pass under the documented
    reading.

    Returns (job_id, step_env), where step_env is the file the queued step
    writes its own $SLURM_JOB_ID.$SLURM_STEP_ID into once it launches."""
    script = Path("wide_hog_and_pending.sh")
    ready = Path("wide_hog_ready")
    step_env = Path("wide_step_env")
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready, srun_args="--exclusive -N2 -n4")
        + "srun --exclusive -N1 -n1 -c2 sh -c "
        + f'\'echo "STEP_ENV=$SLURM_JOB_ID.$SLURM_STEP_ID" > "{step_env}"; '
        + "exec sleep infinity' &\n"
        + "wait\n",
    )
    job_id = atf.submit_job(
        "sbatch", "-N2 -n4 -t5", str(script), wrap_job=False, fatal=True
    )
    assert job_id != 0, "sbatch should submit the job"

    atf.wait_for_step(job_id, 0, fatal=True)
    return job_id, step_env


@requires_id_at_submit
def test_pending_sync_step_shows_real_id_and_reuses_it():
    """A synchronous step queued behind a CPU-hogging step shows a real StepId while
    PENDING and launches with that same ID once the hog is freed."""

    job_id, step_env = _submit_wide_hog_and_pending()
    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"

    # The queued step (.1, submitted second) is visible by a real, numeric
    # StepId (not TBD).
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    # scontrol can address the pending step by that id and reports details for
    # just that step (scontrol.1, "show step"), and nothing is TBD.
    step_show = atf.run_command(f"scontrol -o show step {pending_id}", quiet=True)
    assert (
        step_show["exit_code"] == 0
    ), f"scontrol show step {pending_id} should succeed while pending"
    assert (
        step_show["stdout"].count("StepId=") == 1
    ), f"scontrol show step {pending_id} should report exactly one step"
    assert f"StepId={pending_id}" in step_show["stdout"], (
        f"scontrol show step {pending_id} should describe that step, got: "
        f"{step_show['stdout']!r}"
    )
    assert (
        "State=PENDING" in step_show["stdout"]
    ), f"the queued step should show State=PENDING, got: {step_show['stdout']!r}"
    assert "TBD" not in atf.run_command_output(
        f"scontrol -o show step {job_id}", quiet=True, fatal=True
    ), "no step should render as TBD once a real id is assigned at submit"

    # scontrol.1 documents what a queued step's fields mean: NodeList is the
    # job's whole allocation and CPUs is the task count it requested. Neither
    # describes resources held by the step, which has none until it launches.
    pending_step = atf.get_steps(job_id, quiet=True, fatal=True)[pending_id]
    job_nodes = atf.get_job_parameter(job_id, "NodeList", fatal=True)
    assert pending_step["NodeList"] == job_nodes, (
        f"a queued step's NodeList should be the job's allocation "
        f"{job_nodes!r}, got {pending_step['NodeList']!r}"
    )
    assert str(pending_step["CPUs"]) == "1", (
        f"a queued step's CPUs should be the 1 task it requested, not the 2 "
        f"CPUs it would hold nor the job's 4, got {pending_step['CPUs']!r}"
    )
    # Name and StartTime are unset while queued. The rendering of "unset" is
    # not documented, so assert both change at launch rather than pinning a
    # token scontrol is free to spell differently.
    pending_name = pending_step["Name"]
    pending_start = pending_step["StartTime"]
    assert (
        not pending_name or "sleep" not in pending_name
    ), f"a queued step's Name should not yet be its command, got {pending_name!r}"
    assert not re.match(
        r"\d{4}-\d{2}-\d{2}T\d{2}:", str(pending_start)
    ), f"a queued step's StartTime should not be a timestamp, got {pending_start!r}"

    # Free the resources by cancelling the hog; the pending step launches with
    # the SAME id (PENDING -> RUNNING under the same StepId).
    atf.run_command_exit(f"scancel {hog_id}", quiet=True, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)

    launched_step = atf.get_steps(job_id, quiet=True, fatal=True)[pending_id]
    assert launched_step["Name"] != pending_name, (
        f"the step's Name should be set once it launches, still " f"{pending_name!r}"
    )
    assert launched_step["StartTime"] != pending_start, (
        f"the step's StartTime should be set once it launches, still "
        f"{pending_start!r}"
    )

    # Every check above reads the id back from the controller that assigned
    # it. The task's own environment is the independent view: it shows the id
    # the step actually runs under, not the one the controller recorded.
    atf.assert_file_contents(step_env, f"STEP_ENV={pending_id}", contains=True)


@requires_id_at_submit
def test_pending_sync_step_logs_id_to_stderr():
    """srun prints the assigned StepId to stderr once, when the step first
    goes PENDING; it does not repeat the message on later retries of the
    same step, and never prints it to stdout. It prints a matching started
    notice under the same id when the step launches.

    The streams are kept apart because the notice's stream is part of the
    contract: srun.1 puts this one on stderr, while --async's own submit
    notice goes to stdout."""

    out = Path("log_id.out")
    err = Path("log_id.err")
    ready = Path("log_id_ready")
    script = Path("log_id.sh")
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready)
        + "srun --exclusive -n2 sleep 5\n"
        + "echo STDOUT_ALIVE\n",
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n2 -t2 --output={out} --error={err}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"

    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    # Free the hog; the pending step launches and the job finishes.
    atf.run_command_exit(f"scancel {hog_id}", quiet=True, fatal=True)
    assert atf.wait_for_job_state(
        job_id, "DONE", timeout=60
    ), f"job {job_id} should reach DONE once the hog is cancelled"
    queued_notice = f"StepId={pending_id} queued"
    started_notice = f"StepId={pending_id} started"
    text = _read_when_contains(err, started_notice)
    assert text.count(queued_notice) == 1, (
        f"expected exactly one queued-notice for StepId={pending_id} in "
        f"srun's stderr (not zero, not repeated on retry), got: {text}"
    )
    assert text.count(started_notice) == 1, (
        f"expected exactly one started-notice for StepId={pending_id} in "
        f"srun's stderr once the step launches, got: {text}"
    )
    # The launch notice carries the id the step was queued under, so the
    # pair also shows the id survived the wait rather than being renumbered.
    assert text.index(queued_notice) < text.index(started_notice), (
        f"srun should announce StepId={pending_id} as queued before it "
        f"announces it as started, got: {text}"
    )
    # Anchor on stdout first: without it an absent or unwritten file would
    # satisfy the negative below just as well as a correctly-quiet stdout.
    out_text = _read_when_contains(out, "STDOUT_ALIVE")
    assert queued_notice not in out_text and started_notice not in out_text, (
        f"the queued/started notices belong on stderr; one reached stdout "
        f"instead, got: {out_text}"
    )


@requires_id_at_submit
def test_step_that_starts_immediately_logs_no_queued_or_started_notice():
    """The queued notice describes a step that had to wait, so a step which
    starts right away never prints it. Without this the notice could regress
    into per-step noise on every srun and every positive assertion would still
    pass.

    The started notice is deliberately not asserted absent: srun logs it after
    any retry, including ones this test cannot rule out (a prolog still
    running, a controller RPC timeout), so its absence is not a property of a
    step that started promptly."""

    out = Path("no_notice.out")
    err = Path("no_notice.err")
    script = Path("no_notice.sh")
    atf.make_bash_script(
        script, "srun --exclusive -n2 true\n" + "echo STDERR_ALIVE >&2\n"
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n2 -t2 --output={out} --error={err}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"
    assert atf.wait_for_job_state(
        job_id, "DONE", timeout=60
    ), f"job {job_id} should reach DONE"

    # Anchor on the stream first: an absent or unwritten file would satisfy
    # the negative below just as well as a correctly-quiet srun.
    text = _read_when_contains(err, "STDERR_ALIVE")
    notice = re.search(r"StepId=\S+ queued", text)
    assert not notice, (
        f"a step with resources free should not announce itself as queued, "
        f"got: {text}"
    )


@requires_id_at_submit
@pytest.mark.parametrize(
    "signal_args",
    ["", "--ctld --signal=INT", "--ctld --signal=TERM", "--ctld --signal=KILL"],
)
def test_scancel_pending_step_by_id(signal_args):
    """scancel <jobid>.<stepid> cancels a queued synchronous step before it launches;
    the hogging step keeps running.

    scancel.1 names INT, TERM and KILL as the terminating signals that cancel a
    queued step, so each is exercised alongside the default (no --signal) form.
    All go through the controller, which is the only route to a step that has
    no tasks yet."""

    job_id = _submit_hog_and_pending()
    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    # Cancel just the pending step by id.
    cmd = f"scancel {signal_args} {pending_id}".replace("  ", " ")
    assert (
        atf.run_command_exit(cmd, quiet=True) == 0
    ), f"{cmd} should succeed against a pending step"

    # The pending step is reaped outright, not just moved out of PENDING: a
    # regression that renumbered or relaunched it under another state would
    # otherwise pass.
    for _ in atf.timer():
        states = _steps_by_state(job_id)
        if not any(pending_id in ids for ids in states.values()):
            break
    else:
        assert False, f"pending step {pending_id} should be gone after {cmd}"
    assert hog_id in _steps_by_state(job_id).get(
        "RUNNING", []
    ), f"the hogging step {hog_id} should still be running after the step cancel"


@requires_id_at_submit
def test_cancelled_pending_step_makes_srun_exit_nonzero():
    """Cancelling a queued synchronous step by id pushes the cancel to the waiting
    srun, which aborts with a non-zero exit and logs the cancellation
    (ESLURM_STEP_CANCELLED) instead of hanging or exiting 0."""

    out = Path("cancel_exit.out")
    ready = Path("cancel_exit_ready")
    script = Path("cancel_exit.sh")
    # Hog both CPUs, then run the pending srun in the foreground so its exit
    # status and stderr are captured in the job output. Once it aborts, the
    # script ends and the job terminates the backgrounded hog.
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready)
        + "srun --exclusive -n2 sleep infinity\n"
        + 'echo "PENDING_SRUN_RC=$?"\n',
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n2 -t2 --output={out} --error={out}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"

    pending_id = f"{job_id}.1"
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    # Cancel just the pending step; the waiting srun should be signalled.
    atf.run_command_exit(f"scancel {pending_id}", quiet=True, fatal=True)

    assert atf.wait_for_job_state(
        job_id, "DONE", timeout=60
    ), f"job {job_id} should finish once the pending srun aborts"
    text = _read_when_contains(out, "PENDING_SRUN_RC=")
    assert (
        "PENDING_SRUN_RC=0" not in text
    ), f"the cancelled pending srun should exit non-zero, got: {text}"
    assert (
        "Pending job step cancelled" in text
    ), f"srun should log the pending-step cancellation, got: {text}"


@requires_id_at_submit
def test_nonfatal_signal_leaves_pending_step():
    """A non-terminating signal delivered to a queued synchronous step through the
    controller (scancel --ctld) is a no-op: the placeholder keeps its id and
    the waiting srun still launches it once resources free. Only a terminating
    signal cancels a pending step."""

    job_id = _submit_hog_and_pending()
    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    # A non-fatal signal reaches no tasks and must not reap the placeholder.
    # --ctld routes the signal through the controller (the queued step has no
    # slurmd tasks to signal directly); the controller treats it as a no-op.
    assert (
        atf.run_command_exit(f"scancel --ctld --signal=USR1 {pending_id}", quiet=True)
        == 0
    ), f"scancel --ctld --signal=USR1 {pending_id} should be a no-op success"

    # Give a regressed reap time to land before asserting survival. Under
    # enable_stepmgr the controller forwards the signal to the stepmgr node
    # asynchronously, so scancel returns before the stepmgr has acted and an
    # immediate check would pass even if the placeholder were being reaped.
    # Reaching the timeout is the pass condition here, so xfail keeps atf from
    # logging a spurious "Timer should not timeout" warning on success.
    for _ in atf.timer(timeout=10, xfail=True):
        assert pending_id in _steps_by_state(job_id).get(
            "PENDING", []
        ), f"pending step {pending_id} should survive a non-fatal signal"

    # The waiting srun also survived: freeing the hog launches the same id.
    atf.run_command_exit(f"scancel {hog_id}", quiet=True, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)


@requires_id_at_submit
def test_signal_without_ctld_fails_against_pending_step():
    """scancel.1 requires the controller route for a queued step, so an
    explicit --signal without --ctld must fail rather than misbehave silently.

    Such a signal is sent to the step's nodes directly; a still-PENDING
    placeholder has no slurmstepd on any node yet, so scancel fails with
    "Invalid job id specified" and the placeholder is untouched -- the same
    failure mode as a pending asynchronous step (test_116_55)."""

    job_id = _submit_hog_and_pending()
    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    assert (
        atf.run_command_exit(f"scancel --signal=TERM {pending_id}", quiet=True) != 0
    ), f"scancel --signal=TERM {pending_id} (no --ctld) unexpectedly succeeded"

    # The queued step is untouched: the failed signal never reached it.
    assert pending_id in _steps_by_state(job_id).get(
        "PENDING", []
    ), f"pending step {pending_id} should survive a failed no-ctld signal"

    # The waiting srun also survived: freeing the hog launches the same id.
    atf.run_command_exit(f"scancel {hog_id}", quiet=True, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)


@requires_id_at_submit
def test_step_ids_track_submission_order_without_gaps():
    """A step pending on a busy node keeps the id matching its submission
    order (not its launch order), and the job's three steps occupy a
    contiguous id range with no gaps."""

    # Whole-node (--exclusive) two-node allocation, and non-exact steps that
    # each grab all of a node's CPUs regardless of the node's actual CPU
    # count. Step .0 hogs node0; step .1 (submitted next) targets the busy
    # node0 and must pend; step .2 (submitted last) targets the free node1
    # and runs immediately -- so .2 launches before .1.
    script = Path("submit_order_ids.sh")
    atf.make_bash_script(
        script,
        "nodes=($(scontrol show hostnames $SLURM_JOB_NODELIST))\n"
        "srun -N1 -w ${nodes[0]} sleep infinity &\n"
        + _step_registered_snippet(0)
        + "srun -N1 -w ${nodes[0]} sleep infinity &\n"
        + _step_registered_snippet(1)
        + "srun -N1 -w ${nodes[1]} sleep infinity &\n"
        + "wait\n",
    )
    job_id = atf.submit_job(
        "sbatch", "-N2 --exclusive -t2", str(script), wrap_job=False, fatal=True
    )
    assert job_id != 0, "sbatch should submit the job"

    # Out-of-order window: by this fixed 3-step topology, exactly one step
    # (.1) is PENDING and exactly two (.0, .2) are RUNNING once .2 has
    # launched ahead of the still-queued .1.
    for _ in atf.timer():
        st = _steps_by_state(job_id)
        pending, running = st.get("PENDING", []), st.get("RUNNING", [])
        if len(pending) == 1 and len(running) == 2:
            break
    else:
        assert False, (
            "expected exactly one PENDING step and two RUNNING steps in the "
            "out-of-order window"
        )

    all_nums = sorted(map(_step_num, pending + running))
    assert _step_num(pending[0]) == 1, (
        f"the middle-submitted step should keep id .1 (its submission order), "
        f"got pending step {pending[0]}"
    )
    assert all_nums == [0, 1, 2], (
        f"the job's three steps should have contiguous ids 0, 1, 2 with no "
        f"gaps, got {all_nums}"
    )


@requires_id_at_submit
def test_pending_sync_step_survives_timeout_retry_and_reuses_id():
    """A queued synchronous step whose srun retries on the timeout (no wake poke)
    re-sends carrying its assigned id: the controller purges the lingering
    placeholder and rebuilds one under the SAME id instead of rejecting the
    re-send as a duplicate step id. It stays pending under that id across the
    timeout and still launches with it once resources free.

    Distinct from the reuse test above, which cancels the hog: a step
    completion there wakes/pokes srun and reaps the placeholder, so the re-send
    finds nothing to purge. Here the hog runs across the whole srun timeout
    window, so the re-send is timeout-driven and the placeholder is still
    present -- the only path that exercises the _purge_duplicate_steps delete.
    Also confirms the stderr queued-notice (test_pending_sync_step_logs_id_to_stderr's
    "once, not repeated" claim) still holds across a real timeout re-send,
    which that test's own hog is freed too quickly to exercise.
    """

    # The hog holds the node well past one srun timeout window, so no step
    # completes (no wake) before the queued step's srun re-sends on the timeout.
    out = Path("timeout_retry.out")
    ready = Path("timeout_retry_ready")
    script = Path("timeout_retry.sh")
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready)
        # -v so the queued step's srun surfaces its "still pending" notice,
        # which it logs only when its pending-step wait times out.
        + "srun -v --exclusive -n2 sleep 5 &\n" + "wait\n",
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n2 -t5 --output={out} --error={out}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"

    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    atf.wait_for_step(job_id, 0, fatal=True)

    # The queued step shows a real id while pending.
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    # Wait for positive evidence that srun's wait actually timed out and it
    # re-sent the create: with -v, srun logs the "still pending" notice only on
    # the timeout path. The wait floor is MAX(60, SlurmctldTimeout) plus up to
    # 9s of pid jitter. The hog (sleep infinity, job -t5) cannot end or hit its
    # time limit inside this window, so no step completes and no wake fires: the
    # re-send here is necessarily timeout-driven, not a poke.
    retry_notice = f"StepId={pending_id} still pending"
    for _ in atf.timer(timeout=SLURMCTLD_TIMEOUT + 30):
        if retry_notice in atf.run_command_output(f"cat {out} 2>/dev/null", quiet=True):
            break
    else:
        assert False, (
            f"srun should log {retry_notice!r} once its pending-step wait "
            f"times out and it re-sends the create"
        )

    # The step is still PENDING under the same id: the re-send did not queue a
    # second placeholder, and the srun is still alive (a regressed purge would
    # reject the re-send with ESLURM_DUPLICATE_STEP_ID).
    assert pending_id in _steps_by_state(job_id).get("PENDING", []), (
        f"after the timeout re-send step {pending_id} should still be PENDING "
        f"under the same id"
    )

    # Free the node; the timed-out-and-retried srun launches under the same id.
    atf.run_command_exit(f"scancel {hog_id}", quiet=True, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)

    # The queued-notice was printed exactly once, even though this srun
    # actually re-sent the create on a timeout (not just a hog-cancel poke).
    assert atf.wait_for_job_state(
        job_id, "DONE", timeout=60
    ), f"job {job_id} should reach DONE once the hog is cancelled"
    text = _read_when_contains(out, f"StepId={pending_id} queued")
    assert text.count(f"StepId={pending_id} queued") == 1, (
        f"expected exactly one queued-notice for StepId={pending_id} across "
        f"the timeout re-send, got: {text}"
    )


@requires_id_at_submit
def test_still_pending_notice_requires_verbose():
    """The per-retry "still pending" notice is -v-only. It replaced a line
    that used to print on every retry regardless of verbosity, so staying
    quiet without -v is the contract; the queued notice is not gated and must
    still appear."""

    out = Path("quiet_retry.out")
    err = Path("quiet_retry.err")
    ready = Path("quiet_retry_ready")
    script = Path("quiet_retry.sh")
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready) + "srun --exclusive -n2 sleep 5 &\n" + "wait\n",
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n2 -t5 --output={out} --error={err}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"

    hog_id, pending_id = f"{job_id}.0", f"{job_id}.1"
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.wait_for_step(job_id, 1, state="PENDING", fatal=True)

    # Hold the node past one whole pending-step wait so this srun times out
    # and re-sends at least once with no -v in play. Reaching the timeout is
    # the pass condition, so xfail keeps atf from warning about it.
    for _ in atf.timer(timeout=SLURMCTLD_TIMEOUT + 20, xfail=True):
        assert pending_id in _steps_by_state(job_id).get(
            "PENDING", []
        ), f"step {pending_id} should stay PENDING across the timeout re-send"

    text = atf.run_command_output(f"cat {err} 2>/dev/null", quiet=True)
    assert (
        "still pending" not in text
    ), f"the still-pending retry notice should need -v, got: {text}"
    assert f"StepId={pending_id} queued" in text, (
        f"the queued notice is not -v-gated and should appear without it, "
        f"got: {text}"
    )

    atf.run_command_exit(f"scancel {hog_id}", quiet=True, fatal=True)
    atf.wait_for_step(job_id, 1, fatal=True)


@requires_id_at_submit
def test_het_independent_components_each_get_own_real_id():
    """Independent (non-spanning) steps on different components of a het job
    each pend on their own component's busy resources and each get their own
    real, distinct step id while PENDING.

    Each component is a separate job record, so `scontrol show steps
    <leader_job_id>` only sees the leader's steps. Both pending ids are read
    instead from the stderr announcement each queued step prints for itself."""

    ready0 = Path("het_ready0")
    ready1 = Path("het_ready1")
    pend0_err = Path("het_pend0.err")
    pend1_err = Path("het_pend1.err")
    done0 = Path("het_done0")
    done1 = Path("het_done1")
    out = Path("het_independent.out")
    script = Path("het_independent.sh")
    # Each hog execs its sleep so the sleep is the task itself rather than a
    # child of the wrapping sh; otherwise TERM leaves the sleep running, the
    # hog never frees and the job never completes.
    atf.make_bash_script(
        script,
        f"""srun --het-group=0 --exclusive -n2 sh -c 'touch "{ready0}"; exec sleep infinity' &
srun --het-group=1 --exclusive -n2 sh -c 'touch "{ready1}"; exec sleep infinity' &
for k in $(seq 1 60); do
    [ -f '{ready0}' ] && [ -f '{ready1}' ] && break
    sleep 0.5
done
{{ [ -f '{ready0}' ] && [ -f '{ready1}' ]; }} || {{ echo HOG_NOT_READY; exit 1; }}
srun --het-group=0 --exclusive -n2 sh -c 'touch "{done0}"' 2>'{pend0_err}' &
pend0=$!
srun --het-group=1 --exclusive -n2 sh -c 'touch "{done1}"' 2>'{pend1_err}' &
pend1=$!
wait "$pend0" "$pend1"
echo DONE
""",
    )
    job_id = atf.submit_job_sbatch(
        f"-N1 -n2 -t10 --output={out} --error={out} : -N1 -n2 -t10 {script}",
        fatal=True,
    )
    assert job_id != 0, "het sbatch should submit the job"
    # Each component of a het job is a separate job record; the second
    # component's id is the leader's + 1 (same assumption test_107_3.py's
    # local submit_het_job() helper relies on -- there is no env var or
    # atf helper that reports a sibling component's job id directly).
    comp1_job_id = job_id + 1

    def _queued(path):
        return "queued" in atf.run_command_output(f"cat {path} 2>/dev/null", quiet=True)

    for _ in atf.timer(timeout=60):
        if _queued(pend0_err) and _queued(pend1_err):
            break
    else:
        assert False, (
            "both het components should show a queued pending step; job "
            f"output: {atf.run_command_output(f'cat {out} 2>/dev/null', quiet=True)!r}"
        )

    # Each component's hog is its own RUNNING step, and the queued check above
    # already confirms both hogs are genuinely busy. Discover the ids instead
    # of assuming .0: a het job's step ids come from a counter shared across
    # its components (_set_step_id() draws from het_job->next_step_id), so the
    # components interleave rather than each numbering from zero.
    def _hog_step(component_job_id, tag):
        running = _steps_by_state(component_job_id).get("RUNNING", [])
        assert (
            len(running) == 1
        ), f"{tag} should have exactly one RUNNING step (its hog), got {running}"
        return running[0]

    hog0_id = _hog_step(job_id, "component 0")
    hog1_id = _hog_step(comp1_job_id, "component 1")
    # TERM, not the default SIGKILL: a SIGKILL carrying a real step id against
    # a het leader is re-targeted at every component's whole job, which would
    # tear down the sibling pending placeholder along with the hog.
    atf.run_command_exit(f"scancel --signal=TERM {hog0_id}", quiet=True, fatal=True)
    atf.run_command_exit(f"scancel --signal=TERM {hog1_id}", quiet=True, fatal=True)

    assert atf.wait_for_job_state(
        job_id, "DONE", timeout=150
    ), f"het job {job_id} should reach DONE"
    atf.assert_file_contents(out, "DONE", contains=True)

    for path, tag in ((pend0_err, "component 0"), (pend1_err, "component 1")):
        assert atf.wait_for_file(path), f"{tag}'s pending step never announced an id"
    pend0_text = atf.run_command_output(f"cat {pend0_err}", fatal=True)
    pend1_text = atf.run_command_output(f"cat {pend1_err}", fatal=True)
    id0 = re.search(r"StepId=(\S+) queued", pend0_text)
    id1 = re.search(r"StepId=(\S+) queued", pend1_text)
    assert id0 and id1, (
        "both het components should announce a real StepId while pending, "
        f"got: component 0={pend0_text!r} component 1={pend1_text!r}"
    )
    # Each queued step must announce a real id belonging to its own component
    # job, and the two must differ: a component that inherited its sibling's
    # id, or announced against the wrong component job, fails here. The step
    # numbers themselves are not pinned because het step ids come from a
    # counter shared across the components, so they interleave.
    assert re.fullmatch(rf"{job_id}\.\d+", id0.group(1)), (
        f"component 0's pending step should announce a real id under job "
        f"{job_id}, got {id0.group(1)!r}"
    )
    assert re.fullmatch(rf"{comp1_job_id}\.\d+", id1.group(1)), (
        f"component 1's pending step should announce a real id under job "
        f"{comp1_job_id}, got {id1.group(1)!r}"
    )
    assert id0.group(1) != id1.group(1), (
        f"the two het components' pending steps should have distinct ids, "
        f"both got {id0.group(1)!r}"
    )
    assert atf.wait_for_file(done0) and atf.wait_for_file(done1), (
        "both het-component pending steps should launch once their hog frees "
        "its component's resources"
    )


def test_het_step_component_queues_instead_of_failing():
    """A het step whose second component must queue behind a CPU hog keeps the
    busy-retry path instead of failing outright.

    The second het component inherits the first's step id, so its create
    request carries a non-NO_VAL step id. The controller must not mistake that
    carried-in id for one it assigned at submit and reply
    JOB_PENDING/ESLURM_STEP_QUEUED to an srun that cannot retry it: below the
    26.11 gate that errno is not a launch retry errno, so an old srun would
    give up and the whole het step would fail.

    Runs on both sides of the gate. The queued component is observed by
    State=PENDING rather than by its StepId, which renders as a real id above
    the gate and as TBD below it.
    """

    ready = Path("het_hog_ready")
    ok = Path("het_step_ok")
    fail = Path("het_step_fail")
    script = Path("het_queue.sh")
    atf.make_bash_script(
        script,
        # Hog het-group 1 and signal once its task actually holds the CPUs, so
        # the het step is guaranteed to find group 1 busy when its second
        # component is created (the precondition the regression needs).
        f"srun --het-group=1 --exclusive -n2 sh -c 'touch {ready}; sleep infinity' &\n"
        "hog=$!\n"
        f"for _ in $(seq 1 60); do [ -f {ready} ] && break; sleep 0.5; done\n"
        # Fail loudly if the hog never grabbed group 1 (no contention to test).
        f"[ -f {ready} ] || {{ touch {fail}; exit 1; }}\n"
        # Het step: group 0 is free, group 1 is busy. Its second component
        # inherits the first's step id and hits busy. Launch it, wait for
        # group 1's component to actually register as a pending placeholder --
        # confirming contention was hit, not merely assumed -- then free group
        # 1 so a correctly-retrying step can launch (a buggy controller has
        # already aborted it by now).
        "srun --het-group=0,1 --exclusive true &\n"
        "step=$!\n"
        "comp1=$((SLURM_JOB_ID + 1))\n"
        "comp1_pending=0\n"
        "for _ in $(seq 1 60); do\n"
        "    if scontrol -o show step $comp1 2>/dev/null "
        "| grep -q 'State=PENDING'; then\n"
        "        comp1_pending=1\n"
        "        break\n"
        "    fi\n"
        "    sleep 0.5\n"
        "done\n"
        f'[ "$comp1_pending" = 1 ] || {{ touch {fail}; exit 1; }}\n'
        'kill "$hog" 2>/dev/null\n'
        f"if wait $step; then touch {ok}; else touch {fail}; fi\n",
    )
    job_id = atf.submit_job(
        "sbatch",
        "-N1 -n2 -t2 : -N1 -n2 -t2",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "het sbatch should submit the job"

    # Above the script's own worst case (two 30s waits) plus job startup, so a
    # slow runner reads as a slow runner rather than a product failure.
    for _ in atf.timer(timeout=120):
        if atf.wait_for_file(ok, timeout=1) or atf.wait_for_file(fail, timeout=1):
            break
    else:
        assert (
            False
        ), "the het step should resolve (launch or fail) within the time limit"
    assert ok.exists() and not fail.exists(), (
        "a queued het component must retry and launch, not be told "
        "ESLURM_STEP_QUEUED (which it cannot retry) and abort the het step"
    )


@requires_id_at_submit
def test_sigint_to_queued_srun_reports_cancellation():
    """SIGINT to an srun whose step is still queued reports the cancellation
    and nothing else.

    Pins srun.1's EXAMPLES 10 transcript: the interrupt is answered with
    "Cancelled pending job step with signal 2" and no longer trailed by the
    "Unable to create step ... Job/step already completing or completed"
    error, which the ESLURM_STEP_CANCELLED early return now skips."""

    perr = Path("sigint_pending.err")
    out = Path("sigint_pending.out")
    ready = Path("sigint_pending_ready")
    script = Path("sigint_pending.sh")
    atf.make_bash_script(
        script,
        _hog_ready_snippet(ready)
        + f"""srun --exclusive -n2 sleep infinity 2>'{perr}' &
pend=$!
for k in $(seq 1 120); do
    grep -q queued '{perr}' 2>/dev/null && break
    sleep 0.5
done
grep -q queued '{perr}' 2>/dev/null || {{ echo STEP_NEVER_QUEUED; exit 1; }}
kill -INT "$pend"
wait "$pend"
echo "PENDING_SRUN_RC=$?"
""",
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n2 -t5 --output={out} --error={out}",
        str(script),
        wrap_job=False,
        fatal=True,
    )
    assert job_id != 0, "sbatch should submit the job"

    # The script drives the whole scenario and exits on its own, so don't
    # sample the short PENDING window or race its kill -INT with a scancel.
    assert atf.wait_for_job_state(
        job_id, "DONE", timeout=120
    ), f"job {job_id} should finish once the interrupted srun exits"

    # Assert the precondition ahead of the message checks, which would
    # otherwise report a step that never queued as a missing srun message.
    assert "STEP_NEVER_QUEUED" not in atf.run_command_output(
        f"cat {out} 2>/dev/null", quiet=True
    ), "the second step never queued, so the interrupt exercised nothing"

    text = _read_when_contains(perr, "Cancelled pending job step")
    assert (
        "Cancelled pending job step with signal 2" in text
    ), f"srun should report the interrupt of its queued step, got: {text}"
    assert (
        "Unable to create step" not in text
    ), f"the interrupted srun should not also report a create failure, got: {text}"

    # srun.1 RETURN VALUE covers both ways a queued step ends: cancelled by
    # scancel, or interrupted here. The scancel half is asserted by
    # test_cancelled_pending_step_makes_srun_exit_nonzero.
    rc_text = _read_when_contains(out, "PENDING_SRUN_RC=")
    assert (
        "PENDING_SRUN_RC=0" not in rc_text
    ), f"the interrupted pending srun should exit non-zero, got: {rc_text}"
