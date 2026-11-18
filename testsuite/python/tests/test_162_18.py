############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""srun --step-dependency: a queued dependent's srun re-sends its request.

A dependent step that stays queued longer than srun's queued-step wait makes
srun re-send its request. The re-send must keep the step that was already
accepted, with its place in the queue and its dependency edges, rather than
create a new one.

srun's queued-step wait is at least 60 seconds, so every test here waits
through two of them. They are split from test_162_14, which covers the
dependency types themselves, to keep each module well within the SQA timeout.
"""

import pytest

import atf

# A dependent must be provably blocked by its dependency rather than by a lack
# of CPUs, so the node has to be able to run two one-CPU steps at once.
MIN_NODE_CPUS = 2

# Every step takes exactly one CPU. Without --exact a step takes the whole
# node, so a second step could never run beside it and resource queueing
# would stand in for the dependency under test.
# %t writes stdout on the task node, bypassing independent srun I/O queues.
SRUN = "srun --exact -n1 --open-mode=append " '--output="$SLURM_JOB_NAME.%t.out"'

# Upper bound on srun's queued-step wait floor, see setup().
SLURMCTLD_TIMEOUT = 60


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="--step-dependency requires a 26.11 srun and allocation",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="step dependency reporting requires a 26.11 scontrol",
    )
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="step dependencies require a 26.11 stepmgr",
    )
    atf.require_nodes(1, [("CPUs", MIN_NODE_CPUS)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # CR_CPU so a step is gated only on CPUs: memory must not be able to make a
    # step pend for a reason these tests would misread as the dependency.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # Dependencies are resolved by the stepmgr on the batch node.
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # srun's queued-step wait is MAX(60, SlurmctldTimeout) plus up to 9s; pin
    # it to that floor so the tests force a re-send in bounded time.
    atf.require_config_parameter("SlurmctldTimeout", SLURMCTLD_TIMEOUT, "<=")
    atf.require_slurm_running()


def _run_script(
    tag, body, expected=(), timeout=120, shape="-N1 --exclusive --mincpus=2"
):
    """Run body as a batch script owning one whole node.

    IN tag - unique basename for this test's script and output files
    IN body - bash body that launches srun steps inside the allocation
    IN expected - markers that must all appear before the output is read
    IN timeout - seconds to wait for the job to finish
    IN shape - sbatch options sizing the allocation
    RET list of combined stdout/stderr lines in the order they were written
    """
    script = f"{tag}.sh"
    out = f"{tag}.0.out"
    atf.make_bash_script(script, body)
    job_id = atf.submit_job_sbatch(
        f"-J {tag} -t5 {shape} --open-mode=append "
        f"--output={out} --error={out} {script}",
        fatal=True,
    )
    # Each script runs several steps in sequence and waits out srun's
    # queued-step timeout, so the default wait is too short.
    atf.wait_for_job_state(job_id, "DONE", timeout=timeout, fatal=True)

    # DONE is the controller's view; re-read until the stepd has flushed
    # everything the assertions depend on.
    lines = []
    for _ in atf.timer(fatal=True):
        lines = atf.run_command_output(f"cat {out}", fatal=True).splitlines()
        if all(marker in "\n".join(lines) for marker in expected):
            break
    return lines


def _assert_ordered(lines, first, second):
    """Assert both markers were written, first before second."""
    assert first in lines, f"{first} never ran"
    assert second in lines, f"{second} never ran"
    assert lines.index(first) < lines.index(
        second
    ), f"{second} ran before {first}, so its step dependency was not honored"


def _held_step(started, release, done, exit_code=0):
    """Return a backgrounded step that announces its start and then waits.

    Start a second such step only once the first runs: step ids are given in
    creation order, and the tests name the targets as .0 and .1.

    IN started - file the step creates once it runs
    IN release - file whose creation lets the step finish
    IN done - marker the step prints as it finishes
    IN exit_code - the step's exit code
    RET the bash line launching the step
    """
    return (
        f"""{SRUN} bash -c 'touch "{started}"; until [ -f "{release}" ]; """
        f"""do sleep 0.2; done; echo {done}; exit {exit_code}' &"""
    )


def _wait_step_gone_snippet(step):
    """Return bash lines that wait until a step of the script's job is gone."""
    return (
        f'while scontrol -o show step "$SLURM_JOB_ID" 2>/dev/null | '
        f'grep -q "StepId=$SLURM_JOB_ID.{step} "; do sleep 0.2; done\n'
    )


@pytest.mark.slow
@pytest.mark.parametrize(
    "blocker_exit, target_dependency, dependency_type",
    [
        (0, "", "after"),
        (1, "--step-dependency=afterok:$SLURM_JOB_ID.0", "afterany"),
    ],
    ids=["target_starts", "target_fails"],
)
def test_resend_keeps_pending_step_order(
    blocker_exit, target_dependency, dependency_type
):
    """Re-sending preserves the accepted placeholder and its dependency edges."""
    expected = ["BLOCKER_DONE", "RESEND_RETAINED", "DEPENDENT_STARTED", "TARGET_RC="]
    if blocker_exit == 0:
        expected += ["TARGET_RAN", "TARGET_DONE"]
    lines = _run_script(
        "dep_reverse",
        # The second timeout proves the first re-send received its queued reply.
        # Its placeholder must still precede the async dependent.
        f"""{_held_step("reverse_blocker_started", "reverse_blocker_release", "BLOCKER_DONE", blocker_exit)}
until [ -f reverse_blocker_started ]; do sleep 0.2; done
{{ {SRUN} -v -c2 {target_dependency} bash -c 'echo TARGET_RAN; until [ -f reverse_target_release ]; do sleep 0.2; done; echo TARGET_DONE'; echo "TARGET_RC=$?"; }} 2> reverse_target.err &
{atf.wait_for_step_snippet(1)}{SRUN} --mpi=none --async --overlap --step-dependency={dependency_type}:$SLURM_JOB_ID.1 --output=reverse_dependent.out --error=reverse_dependent.err bash -c 'touch reverse_dependent_started; until [ -f reverse_dependent_release ]; do sleep 0.2; done; echo DEPENDENT_DONE; touch reverse_target_release'
{atf.wait_for_step_snippet(2)}until [ "$(grep -c 'still pending' reverse_target.err)" -ge 2 ]; do sleep 0.2; done
scontrol -o show step "$SLURM_JOB_ID" | awk -v target="StepId=$SLURM_JOB_ID.1" -v dependent="StepId=$SLURM_JOB_ID.2" '
    $1 == target {{ target_line = NR }}
    $1 == dependent {{ dependent_line = NR }}
    END {{ exit !(target_line && dependent_line && target_line < dependent_line) }}
' || echo RESEND_REORDERED
echo RESEND_RETAINED
touch reverse_blocker_release
until [ -f reverse_dependent_started ]; do sleep 0.2; done
echo DEPENDENT_STARTED
touch reverse_dependent_release
wait
{_wait_step_gone_snippet(2)}""",
        expected,
        # Wait through two queued-step timeouts to observe a completed re-send.
        timeout=2 * SLURMCTLD_TIMEOUT + 120,
        shape="-N1 -n2",
    )

    assert "RESEND_REORDERED" not in lines, "re-send replaced its pending record"
    _assert_ordered(lines, "RESEND_RETAINED", "BLOCKER_DONE")
    _assert_ordered(lines, "BLOCKER_DONE", "DEPENDENT_STARTED")
    atf.assert_file_contents("reverse_dependent.out", "DEPENDENT_DONE", contains=True)
    if blocker_exit == 0:
        assert "TARGET_RC=0" in lines, f"target failed to start: {lines!r}"
        _assert_ordered(lines, "DEPENDENT_STARTED", "TARGET_DONE")
    else:
        assert "TARGET_RAN" not in lines, "a target with a failed dependency ran"
        assert "TARGET_RC=0" not in lines, "the impossible target reported success"
        atf.assert_file_contents(
            "reverse_target.err", "can never be satisfied", contains=True
        )


@pytest.mark.slow
def test_dependency_survives_resend():
    """Accepted edges retain gone targets' outcomes, unlike new requests."""
    lines = _run_script(
        "dep_resend",
        f"""{_held_step("resend_started_a", "resend_release_a", "A_DONE")}
until [ -f resend_started_a ]; do sleep 0.2; done
{_held_step("resend_started_b", "resend_release_b", "B_DONE")}
until [ -f resend_started_b ]; do sleep 0.2; done
{{ {SRUN} -v --step-dependency=afterok:$SLURM_JOB_ID.0,afterok:$SLURM_JOB_ID.1 bash -c 'echo C_DONE'; echo "C_RC=$?"; }} &
{atf.wait_for_step_count_snippet(3)}
touch resend_release_a
until grep -q A_DONE "$SLURM_JOB_NAME.0.out" 2>/dev/null; do sleep 0.2; done
{_wait_step_gone_snippet(0)}{SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0 true 2> resend_late.err
echo "LATE_RC=$?"
for _ in $(seq 1 {(2 * SLURMCTLD_TIMEOUT + 45) * 5}); do
    [ "$(grep -c 'still pending' "$SLURM_JOB_NAME.0.out")" -ge 2 ] && break
    sleep 0.2
done
[ "$(grep -c 'still pending' "$SLURM_JOB_NAME.0.out")" -ge 2 ] || echo NO_RESEND
touch resend_release_b
wait""",
        ["A_DONE", "B_DONE", "C_RC=", "LATE_RC="],
        # Two timeouts establish a completed re-send after A's record is gone.
        timeout=2 * SLURMCTLD_TIMEOUT + 120,
    )

    assert "NO_RESEND" not in lines, "srun never completed its queued re-send"
    assert "LATE_RC=0" not in lines, "a new request reused a gone target's outcome"
    atf.assert_file_contents("resend_late.err", "dependency", contains=True)
    assert "C_RC=0" in lines, f"the re-sent dependent failed: {lines!r}"
    _assert_ordered(lines, "B_DONE", "C_DONE")
