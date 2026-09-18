############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Pending sync step launch under stepmgr (create-at-wake).

When a sync step cannot get its CPUs it pends; once a step completes the
stepmgr (the batch node's slurmstepd) creates the pending step under the
lock and pushes it to the waiting srun (create-at-wake), instead of poking
srun to re-send. Once the whole node is free, an earlier whole-node request
must reserve it before a later one-CPU request. This does not require strict
FIFO when an earlier request still cannot fit, or task completion order.

These tests run with enable_stepmgr so steps are stepmgr-managed. The FIFO
test is the primary acceptance signal: on the old poke-and-re-send model it
flaked (a later 1-CPU step beat an earlier whole-node step ~7/10 runs);
create-at-wake makes it deterministic, so it is asserted across repeated
rounds rather than marked xfail.

test_116_61 covers id-at-submit against the controller's step manager;
test_162_4 covers it under stepmgr. These tests add the stepmgr-managed
ordering contract.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

# Enough CPUs that a 1-CPU step is distinguishable from a whole-node one.
# The scripts size their steps from $SLURM_CPUS_ON_NODE rather than assuming
# this exact width, so a wider node still works.
MIN_NODE_CPUS = 2

# Rounds of the FIFO scenario per job; each round must keep submission order.
FIFO_ROUNDS = 5


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Daemons are always >= every client command, so requiring srun >= 26.11
    # already implies the stepmgr serving the create-at-wake is new enough.
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="Issue 50935: create-at-wake requires a 26.11 srun",
    )
    atf.require_nodes(1, [("CPUs", MIN_NODE_CPUS)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # CR_CPU so steps are gated only on CPUs; memory must not confound the
    # "resources busy" condition these tests rely on.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # Steps are managed by the batch node's slurmstepd (create-at-wake runs
    # there); Contain is required for stepmgr.
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def _run_steps(tag, body, expected):
    """Run body as a batch script owning one whole node.

    IN tag - unique basename for this test's script and output files
    IN body - bash body that launches srun steps inside the allocation
    IN expected - markers that must all appear before the output is read
    RET (job_id, lines): the batch job id and the combined stdout/stderr
        lines in the order they were written
    """
    script = f"{tag}.sh"
    out = f"{tag}.out"
    atf.make_bash_script(script, body)
    job_id = atf.submit_job_sbatch(
        f"-J {tag} -t5 -N1 --exclusive --output={out} --error={out} {script}",
        fatal=True,
    )
    # FIFO_ROUNDS rounds of gated step launches exceed the default timeout.
    atf.wait_for_job_state(job_id, "DONE", timeout=180, fatal=True)

    # DONE is the controller's view; re-read until the stepd has flushed
    # everything the assertions depend on.
    lines = []
    for _ in atf.timer(fatal=True):
        lines = atf.run_command_output(f"cat {out}", fatal=True).splitlines()
        if all(marker in "\n".join(lines) for marker in expected):
            break

    # A pending step must never surface as a duplicate: create-at-wake
    # reconciles a lost-push re-send against the provisional step.
    text = "\n".join(lines)
    assert (
        "ESLURM_DUPLICATE_STEP_ID" not in text and "Duplicate job step id" not in text
    ), "a pending step was rejected as a duplicate"
    return job_id, lines


# ---------------------------------------------------------------------------
# Characterization: the stable, intended contract.
# ---------------------------------------------------------------------------


def test_blocked_step_launches_after_blocker_frees():
    """A step with no free CPUs pends, then launches once the blocking step
    releases its CPUs."""
    held = "blocked_held"
    release = "blocked_release"
    # A holds every CPU until released rather than for a fixed time, so B is
    # guaranteed to be pending before A frees.
    _, lines = _run_steps(
        "blocked",
        f"""srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{held}"; until [ -f "{release}" ]; do sleep 0.2; done; if [ "$SLURM_PROCID" -eq 0 ]; then echo A_END; fi' &
until [ -f '{held}' ]; do sleep 0.2; done
srun -n1 bash -c 'echo B_DONE' &
{atf.wait_for_step_count_snippet(2)}
touch '{release}'
wait""",
        ["A_END", "B_DONE"],
    )

    assert "A_END" in lines, "blocking step A did not finish"
    assert "B_DONE" in lines, "pending step B never launched"
    assert lines.index("B_DONE") > lines.index(
        "A_END"
    ), "pending step B launched before its blocker freed CPUs"


def test_configuring_mark_is_cleared_once_srun_launches():
    """A step created at wake is not left marked CONFIGURING after srun
    launches it.

    A create-at-wake step holds its CPUs and ports before srun has launched
    any task, and reports State=CONFIGURING for that window (documented in
    job_state_codes.html). The window closes as soon as the push is
    delivered, so rather than racing it this pins the invariant that matters:
    a step whose tasks are running must no longer carry the provisional mark,
    which would otherwise hide the base state and keep the reap paths
    eligible to cancel a live step.
    """
    held = "launching_held"
    release = "launching_release"
    running = "launching_b_running"
    b_release = "launching_b_release"
    steps = "launching_steps"
    job_id, lines = _run_steps(
        "launching",
        f"""srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{held}"; until [ -f "{release}" ]; do sleep 0.2; done' &
until [ -f '{held}' ]; do sleep 0.2; done
srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{running}"; until [ -f "{b_release}" ]; do sleep 0.2; done; echo B_DONE' &
{atf.wait_for_step_count_snippet(2)}
touch '{release}'
# B is created at the wake and pushed to its srun. Once B's own task is
# running, srun has consumed that push, so the provisional mark must be gone
# while B still holds its resources.
until [ -f '{running}' ]; do sleep 0.2; done
scontrol -o show step $SLURM_JOB_ID > '{steps}' 2>&1
touch '{b_release}'
wait""",
        ["B_DONE"],
    )

    assert "B_DONE" in lines, "the pending step never launched"
    # Re-poll for content before reading: the redirect creates the file empty.
    atf.assert_file_contents(steps, "StepId=", contains=True)
    steps_text = atf.run_command_output(f"cat {steps}", fatal=True)
    assert "State=CONFIGURING" not in steps_text, (
        "a step whose tasks are already running must not still be marked "
        f"CONFIGURING, got: {steps_text!r}"
    )
    # The snapshot also lists .batch/.extern, which are RUNNING too, so look
    # at B's own line: A took step 0 and B was assigned step 1 at submit.
    b_line = next(
        (line for line in steps_text.splitlines() if f"StepId={job_id}.1 " in line),
        "",
    )
    assert (
        "State=RUNNING" in b_line
    ), f"the launched step should report State=RUNNING, got: {steps_text!r}"


# ---------------------------------------------------------------------------
# Acceptance: create-at-wake launches pending steps in FIFO order.
# ---------------------------------------------------------------------------


def test_pending_steps_launch_in_fifo_order():
    """An earlier whole-node step gets freed CPUs before a later 1-CPU step.

    Repeated across rounds: under create-at-wake the wake allocates the
    earlier (larger) step atomically before the later (smaller) one can take
    the CPUs, so BIG always precedes SMALL.
    """
    held = "fifo_held"
    release = "fifo_release"
    body = ""
    for i in range(FIFO_ROUNDS):
        # A holds the whole node until released (not a fixed sleep), so BIG and
        # SMALL are guaranteed to both be pending on it before it frees. BIG
        # (whole node) is queued before SMALL (1 CPU); when A frees, BIG must
        # win its CPUs over the later SMALL. Drain the previous round's steps
        # first so the count gates have a clean baseline; .batch/.extern have
        # non-numeric ids and never match. Both sentinels are cleared before A
        # starts, so a late task from the previous round cannot pre-satisfy
        # this round's gate.
        body += f"""{atf.wait_for_step_count_snippet(0, op="-eq")}
rm -f '{held}' '{release}'
srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{held}"; until [ -f "{release}" ]; do sleep 0.2; done' &
until [ -f '{held}' ]; do sleep 0.2; done
srun -n"$SLURM_CPUS_ON_NODE" bash -c 'echo BIG_{i}' &
# Wait for BIG to register (A + BIG = 2 steps) before submitting SMALL so
# SMALL is later in step_list, then wait for SMALL too (= 3 steps), so both
# are pending on A before A is released.
{atf.wait_for_step_count_snippet(2)}
srun --exact -n1 bash -c 'echo SMALL_{i}' &
{atf.wait_for_step_count_snippet(3)}
touch '{release}'
wait
"""
    _, lines = _run_steps(
        "fifo",
        body,
        [f"BIG_{i}" for i in range(FIFO_ROUNDS)]
        + [f"SMALL_{i}" for i in range(FIFO_ROUNDS)],
    )

    for i in range(FIFO_ROUNDS):
        big, small = f"BIG_{i}", f"SMALL_{i}"
        assert big in lines and small in lines, f"round {i}: both steps must run"
        assert lines.index(big) < lines.index(
            small
        ), f"round {i}: later 1-CPU step beat the earlier whole-node step"


# ---------------------------------------------------------------------------
# Robustness: a pending step's srun disappearing does not wedge the job.
# ---------------------------------------------------------------------------


def test_pending_srun_killed_does_not_wedge_job():
    """Killing a pending step's srun reclaims its CPUs via the push
    connect-failure early-out (the only reclaim for a vanished srun).

    The kill closes srun's listener, so the create-at-wake push connect is
    refused and the stepmgr reaps the killed step's provisional record at
    once, freeing its CPUs for the later step C. The blocker still finishes,
    C still runs, and the job completes."""
    held = "killed_held"
    release = "killed_release"
    _, lines = _run_steps(
        "killed",
        f"""srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{held}"; until [ -f "{release}" ]; do sleep 0.2; done; if [ "$SLURM_PROCID" -eq 0 ]; then echo A_END; fi' &
until [ -f '{held}' ]; do sleep 0.2; done
# B pends on the busy CPUs; kill its srun only once B is a registered
# pending step (A + B = 2 steps), so its provisional record exists when the
# wake pushes to the now-dead listener and fast path B is the only reclaim.
srun -n"$SLURM_CPUS_ON_NODE" bash -c 'echo B_RAN' &
bpid=$!
{atf.wait_for_step_count_snippet(2)}
kill -9 "$bpid" || exit 1
wait "$bpid" || true
# C is submitted after B exits and closes its listener.
# Gate on C being registered before releasing A, so A cannot free while C is
# still racing its own srun startup and C is genuinely pending on it.
srun -n1 bash -c 'echo C_DONE' &
{atf.wait_for_step_count_snippet(3)}
touch '{release}'
wait""",
        ["A_END", "C_DONE"],
    )

    assert "A_END" in lines, "blocking step did not finish after srun kill"
    assert "C_DONE" in lines, "a later step did not run after a pending srun was killed"
    assert "B_RAN" not in lines, "killed step should not have produced output"


def test_immediate_give_up_cancels_queued_step():
    """srun --immediate expiring on a queued step cancels the assigned step.

    A queued step holds a placeholder with a real id; when srun stops
    waiting it must cancel that id, or the placeholder leaks and the next
    wake would hand a real allocation to a step nobody launches. After the
    immediate srun fails, only the blocker may remain, and a later step
    must still launch once the blocker frees.
    """
    held = "imm_held"
    release = "imm_release"
    immediate_err = "immediate.err"
    job_id, lines = _run_steps(
        "immediate",
        f"""srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{held}"; until [ -f "{release}" ]; do sleep 0.2; done' &
until [ -f '{held}' ]; do sleep 0.2; done
# Whole-node step: pends behind the blocker. --immediate=1 makes srun stop
# waiting after ~1s and cancel the placeholder on the way out.
srun --immediate=1 -n"$SLURM_CPUS_ON_NODE" true 2>'{immediate_err}'
echo IMM_RC=$?
# The give-up must cancel the placeholder: only the blocker may remain, so
# the step count drops back to 1 rather than a placeholder lingering.
{atf.wait_for_step_count_snippet(1, op="-eq")}
touch '{release}'
# Resources were not leaked: a normal step launches immediately after.
srun -n1 bash -c 'echo C_DONE'
wait""",
        ["IMM_RC=", "C_DONE"],
    )

    atf.assert_file_contents(immediate_err, f"StepId={job_id}.1 queued", contains=True)
    rc_lines = [line for line in lines if line.startswith("IMM_RC=")]
    assert rc_lines and rc_lines[0] != "IMM_RC=0", (
        "srun --immediate must fail when its step stays queued, " f"got: {rc_lines!r}"
    )
    assert "C_DONE" in lines, "a later step did not run after an immediate give-up"
