############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify the targets swait accepts for a per-step wait, and what it reports.

Covers the <target>.<stepid> forms (numeric jobid, SLUID, array task, the
srun --async --parsable handoff), the fast returns for a step that never
existed or was already reaped, --timeout and --timeout=0, the server-side
rejection of a bare het-step target, and the per-job subscriber-slots cap.
"""

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
    # 2 nodes: a local heterogeneous step (srun A : B,
    # test_het_step_target_rejected) always splits its MPMD groups across
    # disjoint nodes (_handle_het_step_exclude() excludes every node already
    # claimed by an earlier group, unconditionally -- --overlap does not change
    # this). The 4 CPUs are the shape the whole test_162_5..8 family requires,
    # kept identical here so the suite does not reconfigure between them.
    atf.require_nodes(2, [("CPUs", 4)])
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


# Raised over atf's 45s default: the test_162_5..8 family parks several
# concurrent steps, so a step can take longer than that to appear.
STEP_WAIT_SECS = 60

# Long enough that swait subscribes while the step is still running.
# wait_for_step() (1s poll) plus the stepmgr lookup (a scontrol
# round-trip) can consume several seconds before swait even starts;
# give generous headroom.
STEP_SECS = 15
# Only has to separate a real wait from a fast return (~0.2s), so keep it far
# below STEP_SECS; subscribe setup eats an unpredictable slice of the step.
FLOOR_SECS = 1
CEILING_SECS = STEP_SECS + 8
# A fast return does no waiting at all, so allow only process startup plus the
# subscribe round trip. Kept well under STEP_SECS so a wait cannot pass as one.
FAST_RETURN_SECS = 5

# run_command timeout for a swait that blocks on a step: STEP_SECS plus job
# startup and the subscribe round trip, with headroom for a loaded runner.
# Raised over atf's 60s default so an outer kill cannot mask a real wait.
BLOCKING_CMD_SECS = 180

# stepmgr caps subscribers per job at MAX_SUBSCRIBERS.
MAX_SUBSCRIBERS = 64

# Script exit code meaning "the forked swait processes never settled".
SETTLE_TIMEOUT_RC = 4


@pytest.mark.parametrize("resolution", ["env", "ctld"])
def test_step_wait_reports_target_step(resolution):
    """swait <jobid>.0 blocks until step 0 ends and reports its result.

    Run both ways swait can find the stepmgr: the SLURM_STEPMGR fast path
    and the controller lookup a login-node invocation takes.
    """

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_5_step "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    if resolution == "env":
        command = f"swait {job_id}.0"
        env_vars = f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}"
    else:
        command = (
            "env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID "
            f"swait {job_id}.0"
        )
        env_vars = None
    result = atf.run_command(
        command,
        env_vars=env_vars,
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for step 0 end"
    assert (
        f"StepId={job_id}.0" in result["stdout"]
    ), f"missing step line; stdout: {result['stdout']!r}"
    assert (
        "code=0:0" in result["stdout"]
    ), f"expected clean exit code; stdout: {result['stdout']!r}"
    assert (
        "reason=" not in result["stdout"]
    ), f"a cleanly-completed step must not emit a reason; got: {result['stdout']!r}"


def test_step_wait_reports_nonzero_exit():
    """The reported exit code reflects the step's real exit status."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_5_rc "
        f"--wrap 'srun -n1 sh -c \"sleep {STEP_SECS}; exit 7\"'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, "swait rc is wait-outcome, not the step's"
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for step 0 end"
    assert (
        "code=7:0" in result["stdout"]
    ), f"expected code=7:0; stdout: {result['stdout']!r}"


def test_step_wait_via_sluid_target():
    """swait <sluid>.0 blocks until step 0 ends and reports its result,
    exercising the SLUID branch of the same <target>.<stepid> parser a
    numeric jobid target uses."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_5_sluid_step "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid, f"no SLUID for job {job_id}"
    result = atf.run_command(
        f"swait {sluid}.0",
        env_vars=f"SLURM_JOB_SLUID={sluid} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for step 0 end"
    assert (
        f"StepId={job_id}.0" in result["stdout"]
    ), f"missing step line; stdout: {result['stdout']!r}"
    assert (
        "code=0:0" in result["stdout"]
    ), f"expected clean exit code; stdout: {result['stdout']!r}"


def test_step_wait_array_task_target():
    """swait <master>_<task>.<stepid> waits on one step of one array task:
    the ctld per-task lookup rewrites job_id but leaves step_id alone."""

    job_id = atf.submit_job_sbatch(
        "--array=0-1 -N1 --time=5:00 --job-name=test_162_5_array_step "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    # Sub-second polling: this test skips the env fast path, so ctld lookup
    # and array-task discovery already eat into the FLOOR_SECS margin.
    atf.wait_for_step(f"{job_id}_0", 0, timeout=120, poll_interval=0.5, fatal=True)
    task_id = atf.get_job_id_from_array_task(job_id, 0, fatal=True)
    result = atf.run_command(
        "env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID "
        f"swait {job_id}_0.0 --timeout 30",
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for step 0 end"
    # The rewritten target must be task 0's own job id, with step_id untouched.
    assert (
        f"StepId={task_id}.0 code=" in result["stdout"]
    ), f"expected task 0's step line (StepId={task_id}.0); got: {result['stdout']!r}"
    assert (
        "steps drained" not in result["stdout"]
    ), f"expected STEP mode, not DRAIN mode; got: {result['stdout']!r}"


@pytest.mark.slow
def test_parsable_step_id_is_a_swait_target():
    """srun --async --parsable prints <jobid>.<stepid>, which is exactly the
    target swait accepts. Pins the handoff the documented batch-script idiom
    is built on; neither command's own tests cover the pair."""

    out = "parsable.out"
    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_5_parsable "
        "--wrap '"
        f'step=$(srun --async --parsable -n1 sh -c "sleep {STEP_SECS}; exit 7"); '
        f'swait "$step" > {out} 2>&1; exit $?\'',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=120, fatal=False
    ), "job did not complete; swait did not accept srun --parsable's step id"
    atf.assert_file_contents(
        out,
        "code=7:0",
        contains=True,
        message="expected the target step's exit code from the parsable id",
    )


def test_step_wait_live_drain_unobserved_step():
    """A subscribed STEP wait whose target never launched exits 3 when the
    set drains, rather than reading the drain terminator as success.

    Distinct from the fast-return rc-3 cases above: swait subscribes, blocks,
    and only then learns the set drained without its target being reported.
    A hog step holds all of the node's CPUs so the synchronous srun stays
    queued; swait subscribes to the queued step's id, then the hog is
    cancelled and the set drains out from under the subscriber.
    """

    HOG_SECS = 60
    # The hog is backgrounded so the shell reaches the synchronous srun
    # while the hog still runs: queued behind it (the -n4 hog takes every
    # CPU), the sync srun registers a PENDING step. The hog's head start
    # makes it step .0, as get_pending_async_step()'s skip of .0 assumes.
    job_id = atf.submit_job_sbatch(
        "-N1 -n4 --time=5:00 --job-name=test_162_5_drain_unobserved "
        f"--wrap 'srun -n4 sleep {HOG_SECS} & sleep 2; srun -n1 true & wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    # The queued second srun is the job's only pending step besides .0,
    # which get_pending_async_step() deliberately skips.
    step = atf.get_pending_async_step(job_id)
    assert step, f"no queued step found behind the hog for job {job_id}"

    out_file = "drain_unobserved.out"
    err_file = "drain_unobserved.err"
    # Background swait on the queued step, wait for its subscribe to land,
    # then kill the hog so the set drains with the target never launched.
    # -v writes the subscribe sentinel to stderr, keeping stdout clean for
    # the empty-output assertion.
    script = (
        f"env SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)} "
        f"swait -v --timeout=30 {step} >{out_file} 2>{err_file} & sw=$!; "
        + atf.swait_await_subscribe(err_file)
        + f"scancel {job_id}.0; wait $sw"
    )
    result = atf.run_command(script, timeout=BLOCKING_CMD_SECS)
    assert result["exit_code"] == 3, (
        f"swait exited {result['exit_code']}; expected 3, the drain race must "
        f"not read as success; stderr: {result['stderr']!r}"
        f"; {atf.SWAIT_SUBSCRIBE_TIMEOUT_RC} means the subscribe sentinel "
        f"{atf.SWAIT_SUBSCRIBED!r} never appeared in swait -v output, which is a "
        f"harness/log-text problem rather than a notification failure"
    )
    # No duration floor: what distinguishes this from the subscribe-time
    # fast return is ordering, not elapsed time. A fast return exits 3
    # without printing the subscribe sentinel, which the gate above turns
    # into exit 90; rc 3 here is only reachable after a landed subscribe.
    out = atf.run_command_output(f"cat {out_file}", fatal=True)
    assert out == "", f"an unreported step prints no completion; got: {out!r}"


def test_step_wait_nonexistent_step():
    """swait <jobid>.<unknown-step> returns promptly (rc 3): the stepmgr has
    no such step to wait on, so it fast-returns ESLURM_STEPS_DRAINED rather
    than blocking. No result was ever recorded for the target, so swait
    prints no completion line and reports it as unobserved."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_5_nostep "
        "--wrap 'srun -n1 sleep infinity'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait {job_id}.99",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
    )
    assert result["exit_code"] == 3, f"swait exited {result['exit_code']}"
    assert (
        result["duration"] < FAST_RETURN_SECS
    ), f"swait blocked ({result['duration']:.1f}s); expected a prompt fast-return"
    assert result["stdout"] == "", f"unexpected output: {result['stdout']!r}"


def test_step_wait_already_ended_step():
    """A <jobid>.<stepid> target that ran and was already reaped fast-returns
    rc 3 with no output, indistinguishable from a step that never existed.
    The parsable-handoff idiom depends on this: a step that finishes before
    swait subscribes must not block."""

    # Long enough for wait_for_step() to observe step 0 before it ends: its
    # 1s poll leaves only a few attempts inside the step's lifetime.
    job_id = atf.submit_job_sbatch(
        f"-N1 --time=5:00 --job-name=test_162_5_ended_step "
        f"--wrap 'srun -n1 sleep {STEP_SECS}; sleep infinity'",
        fatal=True,
    )
    # Let step 0 exist first: polling only for its absence is satisfied
    # immediately, before the job starts and while BatchHost is still unset.
    atf.wait_for_job_state(job_id, "RUNNING", timeout=STEP_WAIT_SECS, fatal=True)
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    # Then wait until it has been reaped out of the step list entirely.
    for _ in atf.timer(timeout=60, poll_interval=0.5, quiet=True, fatal=True):
        if f"{job_id}.0" not in atf.get_steps(job_id, quiet=True):
            break
    result = atf.run_command(
        f"swait {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
    )
    assert result["exit_code"] == 3, f"swait exited {result['exit_code']}"
    assert (
        result["duration"] < FAST_RETURN_SECS
    ), f"swait blocked ({result['duration']:.1f}s); expected a prompt fast-return"
    assert result["stdout"] == "", f"unexpected output: {result['stdout']!r}"


def test_step_wait_timeout():
    """--timeout applies to a single-step (STEP) wait: a long-running step
    that outlives the timeout makes swait exit 2."""

    TIMEOUT_SECS = 3
    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_5_steptimeout "
        "--wrap 'srun -n1 sleep infinity'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --timeout {TIMEOUT_SECS} {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=TIMEOUT_SECS + 30,
        xfail=True,
    )
    assert (
        result["exit_code"] == 2
    ), f"swait exited {result['exit_code']}, expected 2 (timeout)"
    assert (
        TIMEOUT_SECS - 1 <= result["duration"] < TIMEOUT_SECS + 10
    ), f"swait --timeout={TIMEOUT_SECS} returned in {result['duration']:.1f}s"


def test_timeout_zero_blocks_indefinitely():
    """--timeout=0 disables the deadline: swait waits for the step to end
    and exits 0, rather than treating 0 as an immediate expiry (rc 2)."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_5_timeout_zero "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --timeout=0 {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert (
        result["exit_code"] == 0
    ), f"--timeout=0 must not expire; swait exited {result['exit_code']}"
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for step 0 end"
    assert (
        f"StepId={job_id}.0 code=" in result["stdout"]
    ), f"missing the step line; stdout: {result['stdout']!r}"


def test_het_step_target_rejected():
    """swait <jobid>.<stepid> (no +N) against a local heterogeneous step
    is rejected server-side: a bare target's step_het_comp is NO_VAL,
    which would otherwise wildcard-match whichever component
    find_step_record() returns first."""

    job_id = atf.submit_job_sbatch(
        "-N2 --time=5:00 --job-name=test_162_5_het_reject "
        "--wrap 'srun -n1 sleep 20 : -n1 sleep 20'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --timeout 10 {job_id}.0",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        xfail=True,
        # Fail fast: the stepmgr must reject this outright, so a run that
        # reaches --timeout 10 has already failed the test.
        timeout=30,
    )
    assert result["exit_code"] == 1, f"swait exited {result['exit_code']}, expected 1"
    assert (
        "not supported" in result["stderr"].lower()
    ), f"unexpected stderr: {result['stderr']!r}"


@pytest.mark.slow
def test_subscriber_slots_full():
    """Past MAX_SUBSCRIBERS per-job slots, a further subscribe is rejected
    (exit 1)."""

    surplus = MAX_SUBSCRIBERS + 16
    job_id = atf.submit_job_sbatch(
        "-N1 --time=10:00 --job-name=test_162_5_slots "
        "--wrap 'srun -n1 sleep infinity'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    stepmgr = atf.get_stepmgr_host(job_id)
    slots_dir = "slots"
    # run_command runs in a bash subshell, so the loop/background/wait work.
    # Poll until every forked swait has settled -- either it exited (.rc) or it
    # subscribed (-v line in .out) -- rather than sleeping a fixed interval: a
    # straggler subscribing after the scancel fast-returns 0 and inflates the
    # accepted count.
    # --timeout bounds every forked swait: without it a subscriber that is
    # never notified blocks forever, holding the inherited stdout/stderr pipes
    # open so run_command's own timeout cannot reap the subshells. It must
    # exceed the settle poll below (<=60s): a subscriber that times out exits
    # 2, which matches neither the ^0$ nor the ^1$ grep, so the accepted +
    # rejected == surplus assertion would fail and blame the subscribe logic.
    script = (
        f"mkdir -p {slots_dir}; "
        f"for i in $(seq {surplus}); do "
        f"( env SLURM_JOB_ID={job_id} SLURM_STEPMGR={stepmgr} "
        f"swait -v --timeout=90 {job_id} >{slots_dir}/$i.out 2>&1; "
        f"echo $? >{slots_dir}/$i.rc ) </dev/null & "
        f"done; "
        f"for _ in $(seq 120); do "
        f"n=0; "
        f"for i in $(seq {surplus}); do "
        f"if [ -f {slots_dir}/$i.rc ] || "
        f"grep -q '{atf.SWAIT_SUBSCRIBED}' {slots_dir}/$i.out 2>/dev/null; "
        f"then n=$((n+1)); fi; "
        f"done; "
        f"[ $n -ge {surplus} ] && break; "
        f"sleep 0.5; "
        f"done; "
        f"[ $n -ge {surplus} ] || "
        f"{{ echo SETTLE_TIMEOUT n=$n; exit {SETTLE_TIMEOUT_RC}; }}; "
        f"scancel {job_id}; "
        f"wait 2>/dev/null; "
        f"echo REJECTED=$(grep -l '^1$' {slots_dir}/*.rc 2>/dev/null | wc -l); "
        f"echo ACCEPTED=$(grep -l '^0$' {slots_dir}/*.rc 2>/dev/null | wc -l); "
        f"echo SLOTSMSG=$(grep -l 'subscriber slots full' {slots_dir}/*.out "
        f"2>/dev/null | wc -l)"
    )
    # Headroom over the inner --timeout (90s), so an unnotified subscriber
    # still yields counts rather than an outer kill.
    result = atf.run_command(script, timeout=180)
    assert result["exit_code"] == 0, (
        f"subscriber-slot script did not finish (exit {result['exit_code']}; "
        f"{SETTLE_TIMEOUT_RC} means the forked swaits never settled, which is "
        f"a harness timeout rather than a subscribe-cap failure); "
        f"stdout: {result['stdout']!r} stderr: {result['stderr']!r}"
    )
    rejected_match = re.search(r"REJECTED=(\d+)", result["stdout"])
    assert rejected_match, f"no REJECTED count in output: {result['stdout']!r}"
    rejected = int(rejected_match.group(1))
    accepted_match = re.search(r"ACCEPTED=(\d+)", result["stdout"])
    assert accepted_match, f"no ACCEPTED count in output: {result['stdout']!r}"
    accepted = int(accepted_match.group(1))
    # The settle poll above proves all `surplus` swaits were concurrent, so
    # the cap is pinned exactly: a cap that shrank would still satisfy a
    # <= assertion.
    assert (
        accepted == MAX_SUBSCRIBERS
    ), f"accepted {accepted} subscribers, cap is {MAX_SUBSCRIBERS}: {result['stdout']!r}"
    assert rejected == (surplus - MAX_SUBSCRIBERS), (
        f"expected exactly {surplus - MAX_SUBSCRIBERS} rejections (surplus over "
        f"the cap); got {rejected}: {result['stdout']!r}"
    )
    assert accepted + rejected == surplus, (
        f"expected every subscribe to resolve accepted or rejected; "
        f"accepted={accepted} rejected={rejected} surplus={surplus}: {result['stdout']!r}"
    )
    slotsmsg_match = re.search(r"SLOTSMSG=(\d+)", result["stdout"])
    assert slotsmsg_match, f"no SLOTSMSG count in output: {result['stdout']!r}"
    slotsmsg = int(slotsmsg_match.group(1))
    assert slotsmsg == rejected, (
        f"expected every rejection to report 'subscriber slots full'; "
        f"got {slotsmsg} of {rejected}: {result['stdout']!r}"
    )
