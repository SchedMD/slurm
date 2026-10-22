############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""srun --step-dependency: one step of a job waits on other steps of it.

A step submitted with --step-dependency is admitted and queued without any
resources, and runs only once the steps it names have started or finished as
its dependency type requires. A dependency that can never be satisfied -- an
afterok on a step that failed, say -- fails the dependent step instead of
leaving it queued forever.

The four types mirror the job-level ones (see --dependency in srun.1):

  after       the targets have started
  afterany    the targets have terminated, however they ended
  afterok     the targets ran to completion with exit code zero
  afternotok  the targets terminated in some failed state

As for jobs, "," joins the whole expression with AND and "?" joins it with
OR; the two cannot be mixed.

The tests gate on observed step states and on files the tasks create, never
on sleeps: a dependent that is released early would otherwise be reported as
a pass on a slow machine.

test_162_11 covers pending steps that wait on *resources* (create-at-wake and
its FIFO order); these cover pending steps that wait on *other steps*.
"""

import json
import re

import pytest
import yaml

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
    # it to that floor so the re-send test forces a re-send in bounded time.
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
    # Each script runs several steps in sequence, and the re-send test also
    # waits out srun's queued-step timeout, so the default wait is too short.
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


# ---------------------------------------------------------------------------
# The four dependency types.
# ---------------------------------------------------------------------------


def test_afterok_waits_for_successful_target():
    """afterok releases the dependent once the target exits zero."""
    lines = _run_script(
        "dep_afterok",
        # A is held open until B is known to be queued, so a dependent that
        # ignored its dependency would be seen running while A is still alive.
        f"""{_held_step("afterok_started", "afterok_release", "A_DONE")}
until [ -f afterok_started ]; do sleep 0.2; done
{SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'echo B_DONE' &
{atf.wait_for_step_count_snippet(2)}
touch afterok_release
wait""",
        ["A_DONE", "B_DONE"],
    )

    _assert_ordered(lines, "A_DONE", "B_DONE")


def test_after_releases_once_target_starts():
    """after releases the dependent as soon as the target starts, without
    waiting for it to finish, even when the target was already running."""
    lines = _run_script(
        "dep_after",
        # A announces its start and then stays alive until B has run, so B
        # finishing first is only possible if "after" released it at A's start
        # rather than at A's end.
        f"""{_held_step("after_started", "after_release", "A_DONE")}
until [ -f after_started ]; do sleep 0.2; done
{SRUN} --step-dependency=after:$SLURM_JOB_ID.0 bash -c 'echo B_DONE; touch after_release' &
wait""",
        ["A_DONE", "B_DONE"],
    )

    _assert_ordered(lines, "B_DONE", "A_DONE")


def test_after_waits_for_pending_target_to_start():
    """after waits for a queued target to start, not merely become eligible."""
    lines = _run_script(
        "dep_after_pending",
        # B needs both CPUs, so the holder blocks it even after A succeeds.
        # C overlaps B to run alongside it once B actually starts.
        f"""{_held_step("after_pending_started", "after_pending_release", "A_DONE")}
until [ -f after_pending_started ]; do sleep 0.2; done
{_held_step("after_holder_started", "after_holder_release", "HOLDER_DONE")}
until [ -f after_holder_started ]; do sleep 0.2; done
{SRUN} -c2 --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'until [ -f after_pending_release_b ]; do sleep 0.2; done; echo B_DONE' &
{atf.wait_for_step_snippet(2)}{SRUN} --overlap --step-dependency=after:$SLURM_JOB_ID.2 bash -c 'scontrol -o show step "$SLURM_JOB_ID.2" > after_target_running; echo C_DONE; touch after_pending_release_b' &
{atf.wait_for_step_snippet(3)}touch after_pending_release
{_wait_step_gone_snippet(0)}until scontrol -o show step "$SLURM_JOB_ID.2" | grep -q 'Reason=Resources'; do sleep 0.2; done
scontrol -o show step $SLURM_JOB_ID.2 > after_target_pending
scontrol -o show step $SLURM_JOB_ID.3 > after_pending_state
touch after_holder_release
wait""",
        ["A_DONE", "HOLDER_DONE", "B_DONE", "C_DONE"],
        shape="-N1 -n2",
    )

    atf.assert_file_contents("after_target_pending", "State=PENDING", contains=True)
    atf.assert_file_contents("after_target_pending", "Reason=Resources", contains=True)
    atf.assert_file_contents("after_pending_state", "State=PENDING", contains=True)
    atf.assert_file_contents("after_pending_state", "Reason=Dependency ", contains=True)
    atf.assert_file_contents("after_target_running", "State=RUNNING", contains=True)
    _assert_ordered(lines, "A_DONE", "C_DONE")
    _assert_ordered(lines, "HOLDER_DONE", "C_DONE")
    _assert_ordered(lines, "C_DONE", "B_DONE")


def test_afternotok_waits_for_failed_target():
    """afternotok releases the dependent when the target exits non-zero, and
    an afterok on the same failed target never runs."""
    lines = _run_script(
        "dep_afternotok",
        f"""{_held_step("afternotok_started", "afternotok_release", "A_FAILED", 1)}
until [ -f afternotok_started ]; do sleep 0.2; done
{SRUN} --step-dependency=afternotok:$SLURM_JOB_ID.0 bash -c 'echo CLEANUP_RAN' &
{{ {SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'echo SHOULD_NOT_RUN'; echo "AFTEROK_RC=$?"; }} &
{atf.wait_for_step_count_snippet(3)}
touch afternotok_release
wait""",
        ["A_FAILED", "CLEANUP_RAN", "AFTEROK_RC="],
    )

    _assert_ordered(lines, "A_FAILED", "CLEANUP_RAN")
    assert (
        "SHOULD_NOT_RUN" not in lines
    ), "an afterok dependent ran even though its target failed"
    assert (
        "AFTEROK_RC=0" not in lines
    ), "srun reported success for a step that never ran"


@pytest.mark.parametrize("depend_type", ["afterany:", ""], ids=["explicit", "default"])
@pytest.mark.parametrize("target_exit", [0, 1], ids=["success", "failure"])
def test_afterany_runs_after_target(depend_type, target_exit):
    """Explicit and default afterany wait for termination, regardless of success."""
    lines = _run_script(
        "dep_afterany",
        f"""{_held_step("afterany_started", "afterany_release", "A_DONE", target_exit)}
until [ -f afterany_started ]; do sleep 0.2; done
{SRUN} --step-dependency={depend_type}$SLURM_JOB_ID.0 bash -c 'echo B_DONE' &
{atf.wait_for_step_count_snippet(2)}
touch afterany_release
wait""",
        ["A_DONE", "B_DONE"],
    )

    _assert_ordered(lines, "A_DONE", "B_DONE")


# ---------------------------------------------------------------------------
# Shared job/step dependency decisions (test37.17's local dependency cases).
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("dependency_scope", ["job", "step"])
@pytest.mark.parametrize(
    "dependency_type, target_exit, should_run",
    [
        ("after", 0, True),
        ("after", 1, True),
        ("afterany", 0, True),
        ("afterany", 1, True),
        ("afterok", 0, True),
        ("afterok", 1, False),
        ("afternotok", 0, False),
        ("afternotok", 1, True),
    ],
)
def test_job_and_step_dependencies_share_decisions(
    dependency_scope, dependency_type, target_exit, should_run
):
    """Jobs and steps share waiting/release rules, not impossible final states."""
    atf.make_bash_script(
        "parity_target.sh",
        "echo STARTED > parity_target_started\n"
        "until [ -f parity_target_release ]; do sleep 0.2; done\n"
        f"echo TARGET_DONE\nexit {target_exit}",
    )
    atf.make_bash_script(
        "parity_dependent.sh", "echo DEPENDENT_RAN\necho DONE > parity_dependent_done"
    )

    if dependency_scope == "job":
        # One CPU per job leaves room for an after dependent beside its target.
        job_options = (
            "-N1 -n1 -t2 --output=dep_parity.out --error=dep_parity.out "
            "--open-mode=append"
        )
        target_id = atf.submit_job_sbatch(f"{job_options} parity_target.sh", fatal=True)
        atf.assert_file_contents("parity_target_started", "STARTED")
        dependent_id = atf.submit_job_sbatch(
            f"{job_options} --dependency={dependency_type}:{target_id} "
            "--kill-on-invalid-dep=no parity_dependent.sh",
            fatal=True,
        )
        if dependency_type == "after":
            atf.assert_file_contents("parity_dependent_done", "DONE")
        else:
            atf.wait_for_job_state(
                dependent_id, "PENDING", desired_reason="Dependency", fatal=True
            )
        atf.run_command("touch parity_target_release", fatal=True)
        atf.wait_for_job_state(target_id, "DONE", fatal=True)
        if should_run:
            atf.wait_for_job_state(dependent_id, "DONE", fatal=True)
            atf.assert_file_contents("dep_parity.out", "DEPENDENT_RAN", contains=True)
        else:
            atf.wait_for_job_state(
                dependent_id,
                "PENDING",
                desired_reason="DependencyNeverSatisfied",
                fatal=True,
            )
        atf.assert_file_contents("dep_parity.out", "TARGET_DONE", contains=True)
        lines = atf.run_command_output("cat dep_parity.out", fatal=True).splitlines()
    else:
        if dependency_type == "after":
            before_release = "until [ -f parity_dependent_done ]; do sleep 0.2; done\n"
        else:
            before_release = (
                atf.wait_for_step_snippet(1)
                + "scontrol -o show step $SLURM_JOB_ID.1 > parity_state\n"
            )
        lines = _run_script(
            "dep_parity",
            f"""{SRUN} ./parity_target.sh &
until [ -f parity_target_started ]; do sleep 0.2; done
{{ {SRUN} --step-dependency={dependency_type}:$SLURM_JOB_ID.0 ./parity_dependent.sh; echo "DEPENDENT_RC=$?"; }} &
{before_release}touch parity_target_release
wait""",
            [
                "TARGET_DONE",
                "DEPENDENT_RC=",
                "DEPENDENT_RAN" if should_run else "can never be satisfied",
            ],
        )
        if dependency_type != "after":
            atf.assert_file_contents("parity_state", "State=PENDING", contains=True)
            atf.assert_file_contents(
                "parity_state", "Reason=Dependency ", contains=True
            )
        if not should_run:
            assert "DEPENDENT_RC=0" not in lines, "an impossible step reported success"
            assert any("can never be satisfied" in line for line in lines), (
                "an impossible step should report dependency failure, "
                f"got: {lines!r}"
            )

    assert ("DEPENDENT_RAN" in lines) == should_run, (
        f"{dependency_scope} {dependency_type} on exit {target_exit}: "
        f"expected should_run={should_run}, got: {lines!r}"
    )
    if should_run:
        if dependency_type == "after":
            _assert_ordered(lines, "DEPENDENT_RAN", "TARGET_DONE")
        else:
            _assert_ordered(lines, "TARGET_DONE", "DEPENDENT_RAN")


# ---------------------------------------------------------------------------
# The dependent is blocked by its dependency, not by resources.
# ---------------------------------------------------------------------------


def test_dependent_pends_while_cpus_are_free():
    """A dependent stays PENDING with free CPUs, holding no resources.

    This is the property that separates a step dependency from the existing
    resource-busy queueing: the node can run both steps at once, so the only
    thing keeping the dependent queued is its dependency.
    """
    steps = "free_steps"
    lines = _run_script(
        "dep_free",
        # A takes one CPU of at least two, so B could run immediately if it
        # were not waiting on A.
        f"""{_held_step("free_started", "free_release", "A_DONE")}
until [ -f free_started ]; do sleep 0.2; done
{SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'echo B_DONE' &
{atf.wait_for_step_count_snippet(2)}
scontrol -o show step $SLURM_JOB_ID > '{steps}' 2>&1
touch free_release
wait""",
        ["A_DONE", "B_DONE"],
    )

    _assert_ordered(lines, "A_DONE", "B_DONE")

    atf.assert_file_contents(steps, "StepId=", contains=True)
    steps_text = atf.run_command_output(f"cat {steps}", fatal=True)
    dependent = [line for line in steps_text.splitlines() if "StepDependency=" in line]
    assert dependent, (
        "the dependent step should report the expression it was submitted "
        f"with, got: {steps_text!r}"
    )
    assert re.search(r"\bState=PENDING\b", dependent[0]), (
        "the dependent should still be PENDING while its target runs, got: "
        f"{dependent[0]!r}"
    )
    assert re.search(r"\bReason=Dependency\b", dependent[0]), (
        "the dependent should wait on its dependency, not resources, got: "
        f"{dependent[0]!r}"
    )
    assert re.search(
        r"\bStepDependency=afterok:\d+\.0\b", dependent[0]
    ), f"the dependency expression should be reported, got: {dependent[0]!r}"


# ---------------------------------------------------------------------------
# AND / OR across several targets.
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "expression, target_exit",
    [
        ("afterok:$SLURM_JOB_ID.0:$SLURM_JOB_ID.1", 0),
        ("afterok:$SLURM_JOB_ID.0,afterok:$SLURM_JOB_ID.1", 0),
        ("$SLURM_JOB_ID.0:$SLURM_JOB_ID.1", 1),
        ("$SLURM_JOB_ID.0,$SLURM_JOB_ID.1", 1),
        ("afterok:$SLURM_JOB_ID.0,$SLURM_JOB_ID.1", 1),
    ],
    ids=["colon_list", "comma", "default_colon", "default_comma", "mixed_types"],
)
def test_and_waits_for_every_target(expression, target_exit):
    """A ":"-joined target list and a ","-separated expression both wait for
    every target."""
    state = "and_state"
    lines = _run_script(
        "dep_and",
        # Once A is gone, C must still be PENDING on B, before B is released.
        f"""{_held_step("and_started_a", "and_release_a", "A_DONE")}
until [ -f and_started_a ]; do sleep 0.2; done
{_held_step("and_started_b", "and_release_b", "B_DONE", target_exit)}
until [ -f and_started_b ]; do sleep 0.2; done
{SRUN} --step-dependency={expression} bash -c 'echo C_DONE' &
{atf.wait_for_step_count_snippet(3)}
touch and_release_a
{_wait_step_gone_snippet(0)}scontrol -o show step $SLURM_JOB_ID.2 > '{state}' 2>&1
touch and_release_b
wait""",
        ["A_DONE", "B_DONE", "C_DONE"],
    )

    _assert_ordered(lines, "A_DONE", "C_DONE")
    _assert_ordered(lines, "B_DONE", "C_DONE")
    state_text = atf.run_command_output(f"cat {state}", fatal=True)
    assert re.search(
        r"\bState=PENDING\b", state_text
    ), f"C should still wait on B once A is gone, got: {state_text!r}"


@pytest.mark.parametrize(
    "expression, target_exit",
    [
        ("afterok:$SLURM_JOB_ID.0?afterok:$SLURM_JOB_ID.1", 0),
        ("$SLURM_JOB_ID.0?$SLURM_JOB_ID.1", 1),
        ("afterok:$SLURM_JOB_ID.0?$SLURM_JOB_ID.1", 1),
    ],
    ids=["explicit", "default", "mixed_types"],
)
def test_or_releases_on_first_satisfied_target(expression, target_exit):
    """A "?"-separated expression is released by its first satisfied target,
    without waiting for the others to resolve."""
    lines = _run_script(
        "dep_or_first",
        # A is held until C has run, so C can only finish first if B alone
        # released it.
        f"""{_held_step("or_first_started_a", "or_first_release_a", "A_DONE")}
until [ -f or_first_started_a ]; do sleep 0.2; done
{_held_step("or_first_started_b", "or_first_release_b", "B_DONE", target_exit)}
until [ -f or_first_started_b ]; do sleep 0.2; done
{SRUN} --step-dependency={expression} bash -c 'echo C_DONE; touch or_first_release_a' &
{atf.wait_for_step_count_snippet(3)}
touch or_first_release_b
wait""",
        ["A_DONE", "B_DONE", "C_DONE"],
    )

    _assert_ordered(lines, "B_DONE", "C_DONE")
    _assert_ordered(lines, "C_DONE", "A_DONE")


def test_or_survives_a_failed_edge():
    """A failed edge alone does not condemn a "?"-separated expression while
    another target may still succeed."""
    lines = _run_script(
        "dep_or_failed",
        # A fails first, while B is still held; C must then still run once B
        # exits zero.
        f"""{_held_step("or_failed_started_a", "or_failed_release_a", "A_FAILED", 1)}
until [ -f or_failed_started_a ]; do sleep 0.2; done
{_held_step("or_failed_started_b", "or_failed_release_b", "B_DONE")}
until [ -f or_failed_started_b ]; do sleep 0.2; done
{SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0?afterok:$SLURM_JOB_ID.1 bash -c 'echo C_DONE' &
{atf.wait_for_step_count_snippet(3)}
touch or_failed_release_a
until grep -q A_FAILED "$SLURM_JOB_NAME.0.out" 2>/dev/null; do sleep 0.2; done
{_wait_step_gone_snippet(0)}touch or_failed_release_b
wait""",
        ["A_FAILED", "B_DONE", "C_DONE"],
    )

    _assert_ordered(lines, "A_FAILED", "C_DONE")
    _assert_ordered(lines, "B_DONE", "C_DONE")


@pytest.mark.parametrize("separator", [",", "?"], ids=["and", "or"])
def test_multi_target_failure_timing(separator):
    """AND fails on the first failed edge; OR waits until all edges fail."""
    if separator == ",":
        after_first_failure = (
            "until [ -f failure_dependent_done ]; do sleep 0.2; done\n"
        )
    else:
        after_first_failure = (
            "scontrol -o show step $SLURM_JOB_ID.2 > failure_pending_state\n"
        )
    lines = _run_script(
        "dep_failure_timing",
        f"""{_held_step("failure_started_a", "failure_release_a", "A_DONE", 1)}
until [ -f failure_started_a ]; do sleep 0.2; done
{_held_step("failure_started_b", "failure_release_b", "B_DONE", 1)}
until [ -f failure_started_b ]; do sleep 0.2; done
{{ {SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0{separator}afterok:$SLURM_JOB_ID.1 echo SHOULD_NOT_RUN; echo "C_RC=$?"; touch failure_dependent_done; }} &
{atf.wait_for_step_snippet(2)}touch failure_release_a
{_wait_step_gone_snippet(0)}{after_first_failure}touch failure_release_b
wait""",
        ["A_DONE", "B_DONE", "C_RC=", "can never be satisfied"],
    )

    assert "SHOULD_NOT_RUN" not in lines, "an impossible dependent ran"
    assert "C_RC=0" not in lines, "an impossible dependent reported success"
    assert any(
        "can never be satisfied" in line for line in lines
    ), f"the failure should be a dependency failure, got: {lines!r}"
    dependent_result = next(line for line in lines if line.startswith("C_RC="))
    _assert_ordered(lines, "A_DONE", dependent_result)
    if separator == ",":
        _assert_ordered(lines, dependent_result, "B_DONE")
    else:
        atf.assert_file_contents(
            "failure_pending_state", "State=PENDING", contains=True
        )
        atf.assert_file_contents(
            "failure_pending_state", "Reason=Dependency ", contains=True
        )
        _assert_ordered(lines, "B_DONE", dependent_result)


# ---------------------------------------------------------------------------
# A dependency that can never be satisfied.
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "depend_type, target_exit",
    [("afterok", 1), ("afternotok", 0)],
    ids=["afterok_on_failure", "afternotok_on_success"],
)
def test_impossible_dependency_fails_the_step(depend_type, target_exit):
    """A dependency that can never be met fails the dependent rather than
    leaving it queued, reports it as FAILED, and releases that dependent's
    own afterany successor."""
    tag = f"dep_impossible_{depend_type}"
    swait_out = f"{tag}.swait"
    swait_err = f"{tag}.swait_err"
    lines = _run_script(
        tag,
        # C chains off B, which can never run. B's failure must propagate, or
        # C would hang until the job's time limit.
        f"""{_held_step(f"{tag}_started", f"{tag}_release", "A_ENDED", target_exit)}
until [ -f {tag}_started ]; do sleep 0.2; done
{{ {SRUN} --step-dependency={depend_type}:$SLURM_JOB_ID.0 bash -c 'echo B_SHOULD_NOT_RUN'; echo "B_RC=$?"; }} &
{atf.wait_for_step_count_snippet(2)}
{SRUN} --step-dependency=afterany:$SLURM_JOB_ID.1 bash -c 'echo C_DONE' &
{atf.wait_for_step_count_snippet(3)}
swait -v --timeout=60 $SLURM_JOB_ID.1 > {swait_out} 2> {swait_err} & SW=$!
{atf.swait_await_subscribe(swait_err, quote='"')}
touch {tag}_release
wait $SW
wait""",
        ["A_ENDED", "B_RC=", "C_DONE"],
    )

    assert "B_SHOULD_NOT_RUN" not in lines, "a step whose dependency failed ran"
    assert "B_RC=0" not in lines, "srun reported success for a step that never ran"
    assert (
        "C_DONE" in lines
    ), "an afterany dependent was not released by its target's dependency failure"
    swait_text = atf.run_command_output(f"cat {swait_out}", fatal=True)
    assert re.search(
        r"StepId=\d+\.1 never-launched reason=FAILED\b", swait_text
    ), f"the failed dependent should be reported as FAILED, got: {swait_text!r}"


def test_immediate_fails_an_unsatisfied_dependency():
    """With --immediate, an unsatisfied dependency fails at once instead of
    queueing the step."""
    lines = _run_script(
        "dep_immediate",
        f"""{_held_step("immediate_started", "immediate_release", "A_DONE")}
until [ -f immediate_started ]; do sleep 0.2; done
{SRUN} -I --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'echo SHOULD_NOT_RUN'
echo "IMMEDIATE_RC=$?"
touch immediate_release
wait""",
        ["IMMEDIATE_RC=", "A_DONE"],
    )

    assert "SHOULD_NOT_RUN" not in lines, "an --immediate dependent ran"
    assert "IMMEDIATE_RC=0" not in lines, "an --immediate dependent did not fail"
    rc_line = next(line for line in lines if line.startswith("IMMEDIATE_RC="))
    # It failed while A was still held, rather than waiting A out.
    _assert_ordered(lines, rc_line, "A_DONE")
    assert any(
        "Step dependency not yet satisfied" in line for line in lines
    ), f"srun should say the dependency was not satisfied, got: {lines!r}"


def test_nested_srun_does_not_inherit_dependency():
    """The dependency is not exported, so an srun inside the dependent step
    does not name the (by then finished) target."""
    lines = _run_script(
        "dep_nested",
        f"""{_held_step("nested_started", "nested_release", "A_DONE")}
until [ -f nested_started ]; do sleep 0.2; done
{SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'srun --overlap -n1 echo NESTED_OK' &
{atf.wait_for_step_count_snippet(2)}
touch nested_release
wait""",
        ["A_DONE", "NESTED_OK"],
    )

    _assert_ordered(lines, "A_DONE", "NESTED_OK")


# ---------------------------------------------------------------------------
# Rejected expressions.
# ---------------------------------------------------------------------------


# Each case is exercised from inside a real allocation, against that
# allocation's own job id, so the expression reaches the parser rather than
# being turned away for having no allocation to name steps of. $JOB is
# substituted with the running job id.
@pytest.mark.parametrize(
    "case, expression",
    [
        ("missing_target", "afterok:"),
        ("target_without_step", "afterok:$JOB"),
        ("trailing_separator", "afterok:$JOB."),
        ("trailing_junk", "afterok:$JOB.0junk"),
        ("unsupported_type", "bogus:$JOB.0"),
        ("unsupported_aftercorr", "aftercorr:$JOB.0"),
        ("unsupported_afterburstbuffer", "afterburstbuffer:$JOB.0"),
        ("unsupported_expand", "expand:$JOB.0"),
        ("unsupported_singleton", "singleton"),
        ("target_does_not_exist", "afterok:$JOB.999"),
        ("target_in_another_job", "afterok:99999999.0"),
        ("mixed_separators", "afterok:$JOB.0,afterany:$JOB.0?afterok:$JOB.0"),
        ("default_without_step", "$JOB"),
        ("default_trailing_junk", "$JOB.0junk"),
        ("default_mixed_separators", "$JOB.0,$JOB.0?$JOB.0"),
        ("unknown_outcome_in_or", "$JOB.999?afterok:$JOB.998"),
        ("signed_job_id", "afterok:+$JOB.0"),
        ("signed_step_id", "afterok:$JOB.+0"),
        ("negative_zero_step_id", "afterok:$JOB.-0"),
        ("job_id_whitespace", "afterok: $JOB.0"),
        ("step_id_whitespace", "afterok:$JOB. 0"),
        ("job_id_overflow", "afterok:$(($JOB + 4294967296)).0"),
        ("step_id_overflow", "afterok:$JOB.4294967296"),
        ("after_delay", "after:$JOB.0+1"),
        ("het_step_id", "afterok:$JOB.0+0"),
        ("empty_het_step_component", "afterok:$JOB.0+"),
        ("array_target", "afterok:$JOB_0.0"),
        ("het_job_target", "afterok:$JOB+0.0"),
        ("special_step_target", "afterok:$JOB.batch"),
        ("extern_step_target", "afterok:$JOB.extern"),
    ],
)
def test_invalid_expression_is_rejected(case, expression):
    """An expression that cannot be honored is refused at submission rather
    than accepted as a step that silently never waits."""
    # $JOB becomes the shell's own job id inside the script, so the expression
    # names this allocation and reaches the parser.
    expanded = expression.replace("$JOB", "${SLURM_JOB_ID}")
    # Step .0 is left running so a rejection cannot be blamed on the target
    # having already finished; only the malformed cases should be refused.
    lines = _run_script(
        f"dep_invalid_{case}",
        f"""{_held_step(f"dep_invalid_started_{case}", f"dep_invalid_release_{case}", "A_DONE")}
until [ -f dep_invalid_started_{case} ]; do sleep 0.2; done
{SRUN} --step-dependency="{expanded}" true
echo "RC=$?"
touch dep_invalid_release_{case}
wait""",
        ["RC="],
    )

    rc_lines = [line for line in lines if line.startswith("RC=")]
    assert rc_lines, f"the step submission did not report a result: {lines!r}"
    assert (
        rc_lines[0] != "RC=0"
    ), f"--step-dependency={expression} should be rejected, got exit 0"
    assert any(
        "Job dependency problem" in line for line in lines
    ), f"the rejection should be a dependency error, got: {lines!r}"


@pytest.mark.parametrize("target_exit", [0, 1], ids=["success", "failure"])
@pytest.mark.parametrize("target_step", [0, 99], ids=["finished", "unknown"])
def test_missing_target_matches_job_dependencies(target_exit, target_step):
    """Missing targets satisfy after/afterany but reject outcome-based types."""
    target = f"$SLURM_JOB_ID.{target_step}"
    lines = _run_script(
        "dep_missing",
        f"""{SRUN} bash -c 'exit {target_exit}'
{_wait_step_gone_snippet(0)}{SRUN} --step-dependency=after:{target} echo AFTER_RAN
{SRUN} --step-dependency=afterany:{target} echo AFTERANY_RAN
{SRUN} --step-dependency={target} echo DEFAULT_RAN
{SRUN} --step-dependency=afterok:{target} echo AFTEROK_SHOULD_NOT_RUN
echo "AFTEROK_RC=$?"
{SRUN} --step-dependency=afternotok:{target} echo AFTERNOTOK_SHOULD_NOT_RUN
echo "AFTERNOTOK_RC=$?"
""",
        ["AFTER_RAN", "AFTERANY_RAN", "DEFAULT_RAN", "AFTEROK_RC=", "AFTERNOTOK_RC="],
    )

    for marker in ("AFTER_RAN", "AFTERANY_RAN", "DEFAULT_RAN"):
        assert (
            marker in lines
        ), f"a dependency on a missing target did not run: {lines!r}"
    for depend_type in ("AFTEROK", "AFTERNOTOK"):
        assert (
            f"{depend_type}_SHOULD_NOT_RUN" not in lines
        ), f"{depend_type} on a missing target ran"
        assert (
            f"{depend_type}_RC=0" not in lines
        ), f"{depend_type} on a missing target was accepted"
    assert any(
        "Job dependency problem" in line for line in lines
    ), f"a missing outcome should be a dependency error, got: {lines!r}"


@pytest.mark.parametrize("output_format", ["json", "yaml"])
@pytest.mark.parametrize("complex_values", [False, True], ids=["simple", "complex"])
def test_step_dependency_shows_job_state_reasons(output_format, complex_values):
    """Text and both parser modes use the same state reasons as jobs."""
    parser_options = "v0.0.46" + ("+complex" if complex_values else "")
    w_start = "$PWD/verdict_wait_started"
    w_release = "$PWD/verdict_wait_release"
    r_start = "$PWD/verdict_ready_started"
    r_release = "$PWD/verdict_ready_release"
    dep_done = "$PWD/verdict_dep_done"
    dep_release = "$PWD/verdict_dep_release"
    lines = _run_script(
        "dep_verdict",
        f"""rm -f {w_start} {w_release} {r_start} {r_release} {dep_done} {dep_release}
# .0 holds a CPU so .1 can only wait on its dependency, not on resources
{SRUN} --async bash -c 'touch {w_start}; until [ -f {w_release} ]; do sleep 0.2; done' &
until [ -f {w_start} ]; do sleep 0.2; done
{SRUN} --async --step-dependency=afterok:$SLURM_JOB_ID.0 true &
until scontrol -o show step "$SLURM_JOB_ID" | grep "StepId=$SLURM_JOB_ID.1 " | grep -q "Reason=Dependency "; do sleep 0.2; done
echo SAW_WAITING
scontrol --{output_format}={parser_options} show step "$SLURM_JOB_ID" > verdict_waiting.{output_format} || echo VERDICT_DUMP_FAILED
touch {w_release}
{_wait_step_gone_snippet(0)}{_wait_step_gone_snippet(1)}# .2 holds every CPU, so .3 with an already-satisfied dependency waits on
# resources and reports ready
srun --exact -n2 --async bash -c 'touch {r_start}; until [ -f {r_release} ]; do sleep 0.2; done' &
until [ -f {r_start} ]; do sleep 0.2; done
{SRUN} --async --step-dependency=afterany:$SLURM_JOB_ID.0 bash -c 'touch {dep_done}; until [ -f {dep_release} ]; do sleep 0.2; done' &
until scontrol -o show step "$SLURM_JOB_ID" | grep "StepId=$SLURM_JOB_ID.3 " | grep -q "Reason=Resources "; do sleep 0.2; done
echo SAW_READY
scontrol --{output_format}={parser_options} show step "$SLURM_JOB_ID" > verdict_ready.{output_format} || echo VERDICT_DUMP_FAILED
touch {r_release}
until [ -f {dep_done} ]; do sleep 0.2; done
scontrol -o show step "$SLURM_JOB_ID.3"
scontrol --{output_format}={parser_options} show step "$SLURM_JOB_ID.3" > verdict_running.{output_format} || echo VERDICT_DUMP_FAILED
echo DEP_RAN
touch {dep_release}
{_wait_step_gone_snippet(3)}""",
        ["SAW_WAITING", "SAW_READY", "DEP_RAN"],
        shape="-N1 -n2",
    )

    assert "SAW_WAITING" in lines, "a dependency-blocked step never read waiting"
    assert "SAW_READY" in lines, "a resource-blocked step never read ready"
    assert "DEP_RAN" in lines, "the satisfied dependent never ran"
    assert (
        "VERDICT_DUMP_FAILED" not in lines
    ), f"scontrol structured output failed: {lines!r}"
    running = [line for line in lines if "StepDependency=" in line]
    assert running, "the running step should retain its dependency expression"
    assert all(
        "Reason=None " in line for line in running
    ), f"a running step should have no pending state reason: {running!r}"

    parse_output = json.loads if output_format == "json" else yaml.safe_load
    for phase, expected_states in (
        ("waiting", {"0": "None", "1": "Dependency"}),
        ("ready", {"2": "None", "3": "Resources"}),
        ("running", {"3": "None"}),
    ):
        text = atf.run_command_output(
            f"cat verdict_{phase}.{output_format}", fatal=True
        )
        steps = {
            step["id"].rsplit(".", 1)[1]: step for step in parse_output(text)["steps"]
        }
        for step_number, expected in expected_states.items():
            assert (
                "state_reason" in steps[step_number]
            ), f"{output_format} {phase} step {step_number} omitted state reason"
            actual = steps[step_number]["state_reason"]
            assert actual == expected, (
                f"{output_format} {phase} step {step_number}: "
                f"expected {expected!r}, got {actual!r}"
            )
        if phase == "running":
            assert steps["3"][
                "step_dependency"
            ], "running step must retain expression after dependencies are satisfied"


def test_step_dependency_requires_an_allocation():
    """The option names steps of an existing job, so it cannot be used by an
    srun that would create its own allocation."""
    result = atf.run_command(
        "env -u SLURM_JOB_ID -u SLURM_JOBID "
        "srun -n1 --step-dependency=afterok:1.0 true",
        quiet=True,
    )
    assert result["exit_code"] != 0, (
        "--step-dependency without an existing allocation should be rejected, "
        "got exit 0"
    )
    assert "allocation" in result["stderr"], (
        "the error should say an allocation is required, got: " f"{result['stderr']!r}"
    )


# srun turns these away while parsing options, before it contacts anything, so
# the job id they name need not exist.
@pytest.mark.parametrize(
    "args",
    [
        "--interactive -n1 {dep} true",
        "--external-launcher -n1 {dep} true",
        "--het-group=0 -n1 {dep} true",
        "-Z -w localhost -n1 {dep} true",
        "-n1 {dep} true : -n1 true",
    ],
    ids=["interactive", "external_launcher", "het_group", "no_alloc", "colon_het"],
)
def test_srun_rejects_unsupported_step(args):
    """srun refuses --step-dependency on a step kind it cannot work for."""
    dep = "--step-dependency=afterok:1.0"
    result = atf.run_command(
        f"srun {args.format(dep=dep)}",
        env_vars="SLURM_JOB_ID=1",
        quiet=True,
    )
    assert result["exit_code"] != 0, f"srun {args} should be rejected, got exit 0"
    assert (
        "--step-dependency" in result["stderr"]
    ), f"the error should name --step-dependency, got: {result['stderr']!r}"


@pytest.mark.parametrize("depend_type", ["after", "afterany", "afterok", "afternotok"])
def test_het_target_is_rejected(depend_type):
    """Reject numeric references to heterogeneous steps in ordinary jobs."""
    atf.require_nodes(2, [("CPUs", MIN_NODE_CPUS)])
    atf.require_config_parameter_excludes("SchedulerParameters", "disable_hetjob_steps")
    atf.require_config_parameter_excludes("SchedulerParameters", "disable_hetero_steps")
    lines = _run_script(
        "dep_het_target",
        # Immediate mode bounds a broken wait; overlap avoids resource rejection.
        f"""srun --exact -N1 -n1 --mpi=none bash -c 'touch het_target_started_a; until [ -f het_target_release ]; do sleep 0.2; done' : --exact -N1 -n1 --mpi=none bash -c 'touch het_target_started_b; until [ -f het_target_release ]; do sleep 0.2; done' &
until [ -f het_target_started_a ] && [ -f het_target_started_b ]; do sleep 0.2; done
{SRUN} -N1 -I --overlap --step-dependency={depend_type}:$SLURM_JOB_ID.0 echo SHOULD_NOT_RUN
echo "RC=$?"
touch het_target_release
wait""",
        ["RC="],
        shape="-N2 --exclusive",
    )
    assert "SHOULD_NOT_RUN" not in lines, "a dependency on a het target ran"
    assert "RC=0" not in lines, "a dependency on a het target was accepted"
    assert any(
        "Job dependency problem" in line for line in lines
    ), f"the het target should be rejected as an invalid dependency, got: {lines!r}"


def test_server_rejects_het_job():
    """The server refuses a dependency in a het job even when srun sends it.

    A plain srun in a het allocation is not a het step, so srun lets it
    through; only the server-side check stands between it and running
    unconstrained. The target is left running so a parser rejection cannot
    stand in for the missing server check."""
    # The scheduler places each het component on its own node.
    atf.require_nodes(2, [("CPUs", MIN_NODE_CPUS)])
    lines = _run_script(
        "dep_het_job",
        f"""{_held_step("het_started", "het_release", "A_DONE")}
until [ -f het_started ]; do sleep 0.2; done
{SRUN} -I --overlap --step-dependency=after:$SLURM_JOB_ID.0 bash -c 'echo SHOULD_NOT_RUN'
echo "RC=$?"
touch het_release
wait""",
        ["RC="],
        shape="-N1 -n1 : -N1 -n1",
    )
    assert "SHOULD_NOT_RUN" not in lines, "a dependency in a het job ran"
    assert "RC=0" not in lines, "srun reported success for a refused step"
    assert any(
        "not supported" in line for line in lines
    ), f"the server should refuse the dependency as unsupported, got: {lines!r}"


# ---------------------------------------------------------------------------
# Cancellation and chains.
# ---------------------------------------------------------------------------


def test_cancelled_target_resolves_every_type():
    """A cancelled target fails afterok and releases afternotok and afterany."""
    lines = _run_script(
        "dep_cancel",
        f"""{SRUN} bash -c 'touch cancel_started; exec sleep infinity' &
until [ -f cancel_started ]; do sleep 0.2; done
{{ {SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'echo OK_RAN'; echo "OK_RC=$?"; }} &
{atf.wait_for_step_snippet(1)}
{SRUN} --step-dependency=afternotok:$SLURM_JOB_ID.0 bash -c 'echo NOTOK_RAN' &
{atf.wait_for_step_snippet(2)}
{SRUN} --step-dependency=afterany:$SLURM_JOB_ID.0 bash -c 'echo ANY_RAN' &
{atf.wait_for_step_count_snippet(4)}
scancel $SLURM_JOB_ID.0
wait""",
        ["OK_RC=", "NOTOK_RAN", "ANY_RAN"],
    )

    assert "OK_RAN" not in lines, "afterok ran after its target was cancelled"
    assert "OK_RC=0" not in lines, "srun reported success for a step that never ran"
    assert "NOTOK_RAN" in lines, "afternotok was not released by a cancellation"
    assert "ANY_RAN" in lines, "afterany was not released by a cancellation"


@pytest.mark.parametrize("dependency_type", ["afterany", "after"])
def test_cancelled_pending_target_releases_dependents(dependency_type):
    """A never-launched cancellation releases terminal and start dependents."""
    lines = _run_script(
        "dep_cancel_pending",
        # B is queued on A and C on B. Once B is cancelled, C must run while A
        # is still held, so C can only finish first if B's cancellation
        # released it.
        f"""{_held_step("cancel_pending_started", "cancel_pending_release", "A_DONE")}
until [ -f cancel_pending_started ]; do sleep 0.2; done
{SRUN} --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'echo B_SHOULD_NOT_RUN' &
{atf.wait_for_step_snippet(1)}
{SRUN} --step-dependency={dependency_type}:$SLURM_JOB_ID.1 bash -c 'echo C_DONE; touch cancel_pending_release' &
{atf.wait_for_step_snippet(2)}
scancel $SLURM_JOB_ID.1
wait""",
        ["A_DONE", "C_DONE"],
    )

    assert "B_SHOULD_NOT_RUN" not in lines, "the cancelled pending step ran"
    _assert_ordered(lines, "C_DONE", "A_DONE")


def test_chain_and_fan_out():
    """A four-deep afterok chain runs in order, and dependents fanned out on
    one target are all released by it."""
    body = f"""{_held_step("chain_started", "chain_release", "S0_DONE")}
until [ -f chain_started ]; do sleep 0.2; done
"""
    # Steps 1-3 chain off their predecessor; 4 and 5 fan out from step 0.
    targets = {1: 0, 2: 1, 3: 2, 4: 0, 5: 0}
    for step, target in targets.items():
        body += (
            f"{SRUN} --step-dependency=afterok:$SLURM_JOB_ID.{target} "
            f"bash -c 'echo S{step}_DONE' &\n"
            f"{atf.wait_for_step_snippet(step)}"
        )
    body += "touch chain_release\nwait"

    markers = [f"S{step}_DONE" for step in range(6)]
    lines = _run_script("dep_chain", body, markers)

    for step, target in targets.items():
        _assert_ordered(lines, f"S{target}_DONE", f"S{step}_DONE")


# ---------------------------------------------------------------------------
# A queued dependent's srun re-sends its request.
# ---------------------------------------------------------------------------


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
