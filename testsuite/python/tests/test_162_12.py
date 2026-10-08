############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a pre-26.11 srun keeps the poke-and-re-send path under a 26.11 stepmgr.

Issue 50935 cross-version guard: create-at-wake is gated on the pending
step's negotiated protocol version, so a below-gate srun must keep the old
behavior. The stepmgr must poke it to re-send rather than create the step
and push SRUN_STEP_ALLOCATED, which such an srun cannot decode and would
wait out until its timeout.

This is the inverse of test_162_11 (which checks the new behavior). It runs
only in a mixed-version setup with srun < 26.11 and daemons >= 26.11 (an
older client against upgraded daemons); it skips otherwise, so a normal
single-version run never executes it. Needs the SQA mixed-version harness.

Delete this module when 26.05 leaves the supported version matrix: the skipif
below can never be true after that, and it would become permanently skipped
dead weight.
"""

import re

import pytest

import atf

# New daemons, old srun: only true in a mixed-version (upgrade) cluster.
pytestmark = [
    pytest.mark.slow,
    pytest.mark.skipif(
        atf.get_version("bin/srun") >= (26, 11),
        reason="Issue 50935: needs a pre-26.11 srun (mixed-version setup)",
    ),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="Issue 50935: cross-version guard needs a 26.11 stepmgr",
    )
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # The step manager runs in the batch node's slurmstepd, which is where
    # create-at-wake would take effect; Contain is required for stepmgr.
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def test_old_srun_pending_step_still_launches_under_stepmgr():
    """A below-gate srun's queued step still launches once its blocker frees.

    The stepmgr must fall back to poking this srun to re-send. If it instead
    pushed the created step, the old srun would never consume it and the step
    would hang until its create timeout.
    """
    held = "old_srun_held"
    release = "old_srun_release"
    out = "old_srun.out"
    script = "old_srun.sh"
    queued_steps = "old_srun_queued_steps"

    # A below-gate srun's queued step carries the SLURM_PENDING_STEP sentinel,
    # rendered <job_id>.TBD, so gate on the state rather than on a numeric
    # StepId that will never appear here.
    atf.make_bash_script(
        script,
        f"""srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{held}"; until [ -f "{release}" ]; do sleep 0.2; done; if [ "$SLURM_PROCID" -eq 0 ]; then echo A_END; fi' &
until [ -f '{held}' ]; do sleep 0.2; done
srun -n1 bash -c 'echo B_DONE' &
for _ in $(seq 1 300); do
    scontrol -o show step $SLURM_JOB_ID 2>/dev/null | grep -q 'State=PENDING' && break
    sleep 0.2
done
scontrol -o show step $SLURM_JOB_ID > '{queued_steps}' 2>&1
grep -q 'State=PENDING' '{queued_steps}' || {{ echo STEP_NOT_PENDING; exit 1; }}
touch '{release}'
wait""",
    )
    job_id = atf.submit_job_sbatch(
        f"-J old_srun -t5 -N1 --exclusive --output={out} --error={out} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=120, fatal=True)

    lines = []
    for _ in atf.timer(fatal=True):
        lines = atf.run_command_output(f"cat {out}", fatal=True).splitlines()
        if "A_END" in lines and "B_DONE" in lines:
            break

    assert "A_END" in lines, "blocking step A did not finish"
    assert "B_DONE" in lines, "a below-gate srun's queued step never launched"
    assert lines.index("B_DONE") > lines.index(
        "A_END"
    ), "the queued step launched before its blocker freed CPUs"

    # scancel.1 and srun.1 promise that a below-gate srun's queued step is
    # shown as <job_id>.TBD, and so can only be cancelled at the job level.
    # Re-poll for content before reading: the redirect creates the file empty.
    atf.assert_file_contents(queued_steps, "StepId=", contains=True)
    queued = atf.run_command_output(f"cat {queued_steps}", fatal=True)
    # Older scontrol releases printed the pending sentinel as TDB.
    labels = ("TBD", "TDB") if atf.get_version("bin/scontrol") < (26, 5) else ("TBD",)
    assert any(
        f"StepId={job_id}.{label}" in queued for label in labels
    ), f"an older srun's queued step should have a pending sentinel, got: {queued}"
    assert (
        f"StepId={job_id}.1" not in queued
    ), f"a pre-26.11 srun's queued step must not get a real StepId, got: {queued}"


# Gate on scontrol, which get_version() uses to tell whether swait exists:
# get_version("bin/swait") on a tree without swait skips the whole module at
# collection, taking the srun test above with it.
@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 5),
    reason="Issue 50940: swait needed for the drain assertion, added in 26.05",
)
def test_old_swait_drains_despite_queued_bystander():
    """A whole-set swait drains when the blocking step ends, even while a
    below-gate srun's step is still queued behind it.

    A queued synchronous step holds the whole-set wait open only when both
    the job's stepmgr and the srun that submitted it are 26.11 or newer, so
    this below-gate bystander must not. The stepmgr checks for a drain as
    the blocker ends, before the poked srun re-sends, so a correct wait
    returns 0 then. A regression that counted the poke-only placeholder as
    blocking would instead wait on the bystander, which outlives swait's
    --timeout.
    """
    held = "old_swait_held"
    release = "old_swait_release"
    result_file = "old_swait_drain.result"
    swait_err = "old_swait_drain.err"
    script = "old_swait_drain.sh"

    atf.make_bash_script(
        script,
        f"""srun -n"$SLURM_CPUS_ON_NODE" bash -c 'touch "{held}"; until [ -f "{release}" ]; do sleep 0.2; done' &
until [ -f '{held}' ]; do sleep 0.2; done
# Bystander: queues behind the blocker as a poke-only placeholder (.TBD);
# once it launches it outlives swait's --timeout.
srun -n1 sleep infinity &
for _ in $(seq 1 300); do
    scontrol -o show step $SLURM_JOB_ID 2>/dev/null | grep -q 'State=PENDING' && break
    sleep 0.2
done
scontrol -o show step $SLURM_JOB_ID 2>/dev/null | grep -q 'State=PENDING' || {{ echo rc=step-not-pending > '{result_file}'; exit 1; }}
swait -v --timeout=60 2>'{swait_err}' &
SW=$!
# Release the blocker only once swait has subscribed.
( {atf.swait_await_subscribe(swait_err)} ) || {{ echo rc=no-subscribe > '{result_file}'; exit 1; }}
touch '{release}'
wait $SW
echo "rc=$?" > '{result_file}'
""",
    )
    atf.submit_job_sbatch(
        f"-J old_swait_drain -t5 -N1 --exclusive {script}",
        fatal=True,
    )

    # Poll the content, not just the file: it may show up before the echo
    # lands in it. A wedged wait (the regression) never writes rc=0: swait
    # hits its own 60s timeout first.
    match = None
    for _ in atf.timer(timeout=180, quiet=True):
        content = atf.run_command_output(f"cat {result_file}", quiet=True)
        match = re.search(r"rc=(\S+)", content)
        if match:
            break
    assert match, "the batch script should report swait's result, but it did not"
    assert match.group(1) == "0", (
        "swait should drain once the blocking step ended, but got "
        f"rc={match.group(1)}: the queued bystander must not hold the wait open"
    )
