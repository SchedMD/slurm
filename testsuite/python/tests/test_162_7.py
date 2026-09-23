############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify swait's --follow stream and the whole-set drain it terminates on.

Covers that a DRAIN subscriber receives no per-step pushes, --follow
streaming one notification per step end across concurrent steps, het-step
components and a never-launched step in that stream, the -Q and --yaml
renderings of it, the in-script (batch-script) usage that must not deadlock,
and the --follow-with-a-step-id usage error.

The --json and --yaml assertions here are about the shape of the stream
(one document per completion, the drain terminator), not about field
content.
"""

import json
import re

import pytest
import yaml

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
    # >1 CPU so multiple -n1 steps run concurrently (test_follow_streams_*).
    # 2 nodes: a local heterogeneous step (srun A : B,
    # test_follow_reports_het_step_components) always splits its MPMD groups
    # across disjoint nodes (_handle_het_step_exclude() excludes every node
    # already claimed by an earlier group, unconditionally -- --overlap does
    # not change this).
    atf.require_nodes(2, [("CPUs", 4)])
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


# Long enough that swait subscribes while the step is still running.
# wait_for_step() (1s poll) plus the stepmgr lookup (a scontrol
# round-trip) can consume several seconds before swait even starts;
# give generous headroom.
STEP_SECS = 15
# Only has to separate a real wait from a fast return (~0.2s), so keep it far
# below STEP_SECS; subscribe setup eats an unpredictable slice of the step.
FLOOR_SECS = 1
CEILING_SECS = STEP_SECS + 8

# run_command timeout for a swait that blocks on a step: STEP_SECS plus job
# startup and the subscribe round trip, with headroom for a loaded runner.
# Raised over atf's 60s default so an outer kill cannot mask a real wait.
BLOCKING_CMD_SECS = 180

# Raised over atf's 45s default: this module parks several concurrent
# steps, so a step can take longer than that to appear.
STEP_WAIT_SECS = 60


def test_drain_json():
    """swait --json <jobid> (DRAIN mode) emits the whole-set drain terminator:
    a srun_steps_drained object whose step_id is the 'entire'-job sentinel."""

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_162_7_drainjson "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --json {job_id}",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for the drain"
    obj = json.loads(result["stdout"].strip())
    # The whole-set drain terminator is the NO_VAL step, dumped as "entire",
    # and carries no per-step data.
    assert (
        atf.get_data_parser_number(obj["step_id"]["job_id"]) == job_id
    ), f"bad json: {result['stdout']!r}"
    assert obj["step_id"]["step_id"] == "entire", f"bad json: {result['stdout']!r}"
    assert "exit_code" not in obj, f"terminator has per-step data: {result['stdout']!r}"
    assert "state" not in obj, f"terminator has per-step data: {result['stdout']!r}"
    # The terminator is emitted bare too, not wrapped in a response object.
    assert not (
        {"meta", "errors", "warnings"} & obj.keys()
    ), f"terminator must carry no response wrapper: {result['stdout']!r}"


@pytest.mark.slow
def test_drain_receives_no_per_step_pushes():
    """A DRAIN subscriber never receives a per-step push: stdout is exactly
    the one summary line, with no intermediate StepId= lines."""

    # Stagger the steps and make the earlier one outlast the subscribe
    # setup (two wait_for_step polls plus a scontrol round trip). If it
    # ended before swait subscribed there would be no per-step push to
    # suppress, and the assertion below would hold vacuously.
    job_id = atf.submit_job_sbatch(
        "-N1 -n2 --time=10:00 --job-name=test_162_7_drain_multi "
        "--wrap '"
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS} & "
        f"sleep 2; srun -n1 --mem=0 --overlap sleep {STEP_SECS * 2} & "
        "wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    atf.wait_for_step(job_id, 1, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait {job_id}",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    # Waking on the earlier step's end rather than on the drain would land
    # well under STEP_SECS, since swait subscribes a few seconds in.
    assert result["duration"] >= STEP_SECS, (
        f"swait returned in {result['duration']:.1f}s; expected to wait past "
        "the earlier step's end for the whole-set drain"
    )
    assert result["stdout"].strip() == f"JobId={job_id} steps drained", (
        f"DRAIN subscriber should see only the summary line, no per-step "
        f"pushes for the earlier-ending step; got: {result['stdout']!r}"
    )


@pytest.mark.slow
def test_quiet_follow_suppresses_drain_line():
    """-Q under --follow suppresses the per-step lines and the whole-set
    drain line too, leaving stdout empty while the wait still succeeds."""

    job_id = atf.submit_job_sbatch(
        f"-N1 -n2 --time=10:00 --job-name=test_162_7_quiet_follow "
        f"--wrap '"
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS // 2} & "
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS} & "
        f"wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    atf.wait_for_step(job_id, 1, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait -Q --follow {job_id}",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    # -Q prints nothing whether swait waited or fast-returned, so bound the
    # duration too; otherwise this test passes without asserting a wait.
    assert (
        FLOOR_SECS <= result["duration"] < CEILING_SECS
    ), f"swait took {result['duration']:.1f}s; expected to wait for the drain"
    assert (
        result["stdout"] == ""
    ), f"-Q must suppress the drain line too; stdout: {result['stdout']!r}"


@pytest.mark.slow
def test_follow_yaml_emits_separate_documents():
    """--follow --yaml emits one YAML *document* per completion, so the
    stream parses with safe_load_all() into more than one document."""

    # Staggered so step 0 ends well before the drain, and its document is
    # therefore never at risk of being dropped with the terminator.
    job_id = atf.submit_job_sbatch(
        f"-N1 -n2 --time=10:00 --job-name=test_162_7_follow_yaml "
        f"--wrap '"
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS} & "
        f"sleep 2; "
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS * 2} & "
        f"wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    atf.wait_for_step(job_id, 1, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --follow --yaml {job_id}",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    docs = [d for d in yaml.safe_load_all(result["stdout"]) if d]
    assert (
        len(docs) >= 2
    ), f"expected a step document plus the drain terminator; got: {result['stdout']!r}"
    # The drain terminator renders step_id.step_id as "entire".
    assert any(
        d["step_id"]["step_id"] == "entire" for d in docs
    ), f"missing the drain terminator document: {result['stdout']!r}"
    assert any(
        d["step_id"]["step_id"] != "entire" for d in docs
    ), f"missing a per-step document: {result['stdout']!r}"


@pytest.mark.slow
@pytest.mark.parametrize("resolution", ["env", "ctld"])
def test_follow_streams_multiple_steps(resolution):
    """--follow emits one line per step end across concurrent steps, then
    the whole-set drain line (see Issue 50928 note below on the last
    step's line).

    Run both ways swait can find the stepmgr: the SLURM_STEPMGR fast path
    and the controller lookup a login-node invocation takes.
    """

    # Three steps, staggered so 0 and 1 both end well before the drain. Two
    # reported completions are what separate a stream from a single event.
    job_id = atf.submit_job_sbatch(
        f"-N1 -n3 --time=10:00 --job-name=test_162_7_multi "
        f"--wrap '"
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS} & "
        f"sleep 2; "
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS * 2} & "
        f"sleep 2; "
        f"srun -n1 --mem=0 --overlap sleep {STEP_SECS * 4} & "
        f"wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    atf.wait_for_step(job_id, 1, timeout=STEP_WAIT_SECS, fatal=True)
    atf.wait_for_step(job_id, 2, timeout=STEP_WAIT_SECS, fatal=True)
    if resolution == "env":
        command = f"swait --follow {job_id}"
        env_vars = f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}"
    else:
        command = (
            "env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID "
            f"swait --follow {job_id}"
        )
        env_vars = None
    result = atf.run_command(
        command,
        env_vars=env_vars,
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    out = result["stdout"]
    # Step 2 ends together with the drain, so its line may be dropped (Issue
    # 50928); steps 0 and 1 end well before and must always be reported.
    assert (
        f"StepId={job_id}.0 code=" in out
    ), f"missing the earlier-ending step's line from --follow; got: {out!r}"
    assert (
        f"StepId={job_id}.1 code=" in out
    ), f"--follow must stream every completion, not just the first; got: {out!r}"
    assert (
        f"JobId={job_id} steps drained" in out
    ), f"expected the whole-set drain line at the end of --follow; got: {out!r}"
    assert not re.search(
        rf"StepId={job_id}\.(batch|extern|interactive)", out
    ), f"--follow reports regular steps only; got: {out!r}"


@pytest.mark.slow
def test_follow_in_batch_script_no_deadlock():
    """swait --follow run from the job's own batch script must terminate
    on the whole-set drain, not job completion, so the script exits and
    the job reaches COMPLETED instead of hanging to walltime."""

    out = "inscript.out"
    job_id = atf.submit_job_sbatch(
        "-N1 --time=2:00 --job-name=test_162_7_inscript "
        f"--wrap 'srun --async -n1 sleep {STEP_SECS}; "
        f"swait --follow $SLURM_JOB_ID > {out} 2>&1; exit $?'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=90, fatal=False
    ), "job did not reach COMPLETED; swait --follow deadlocked or failed in-script"
    atf.assert_file_contents(
        out,
        f"JobId={job_id} steps drained",
        contains=True,
        message="expected the drain line, proving swait followed to drain",
    )
    # The batch and extern steps both exist here, so this is where a special
    # step would leak into the --follow stream if the filter regressed.
    content = atf.run_command_output(f"cat {out}", fatal=True)
    assert not re.search(
        r"StepId=\d+\.(batch|extern|interactive)", content
    ), f"--follow reports regular steps only; got: {content!r}"


def test_follow_with_step_id_usage_error():
    """--follow combined with a step id is rejected before any work (rc 1)."""

    result = atf.run_command(
        "swait --follow 1.0",
        env_vars="SLURM_STEPMGR=localhost",
        xfail=True,
        # Fail fast: this must be rejected at parse time, before swait
        # contacts anything, so a run that blocks has already failed.
        timeout=30,
    )
    assert result["exit_code"] == 1, f"swait exited {result['exit_code']}, expected 1"
    assert (
        "follow" in result["stderr"].lower()
    ), f"unexpected stderr: {result['stderr']!r}"


def test_follow_reports_never_launched():
    """--follow (ALL mode) reports never-launched for a still-pending
    async step cancelled before launch, same as STEP mode, then keeps
    streaming to the whole-set drain."""

    job_id = atf.submit_job_sbatch(
        "-N1 -n4 --time=5:00 --job-name=test_162_7_follow_neverlaunch "
        "--wrap 'srun -n4 sleep infinity & sleep 1; "
        "srun --async -n1 sleep 5; wait'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    pend = atf.get_pending_async_step(job_id)
    assert pend is not None, "expected a pending async step to subscribe to"

    out_file = "follow_neverlaunch.out"
    script = (
        f"env SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)} "
        f"swait -v --follow --timeout=30 {job_id} >{out_file} 2>&1 & sw=$!; "
        + atf.swait_await_subscribe(out_file)
        + f"scancel {pend}; sleep 2; scancel {job_id}; wait $sw"
    )
    result = atf.run_command(script, timeout=BLOCKING_CMD_SECS)
    assert result["exit_code"] == 0, (
        "swait --follow was not notified of the pending step's "
        f"cancellation, or did not drain afterward (exited {result['exit_code']})"
        f"; {atf.SWAIT_SUBSCRIBE_TIMEOUT_RC} means the subscribe sentinel "
        f"{atf.SWAIT_SUBSCRIBED!r} never appeared in swait -v output, which is a "
        f"harness/log-text problem rather than a notification failure"
    )
    out = atf.run_command_output(f"cat {out_file}", fatal=True)
    assert re.search(
        rf"StepId={re.escape(pend)} never-launched reason=(FAILED|CANCELLED)\b", out
    ), f"--follow did not report the documented never-launched line: {out!r}"
    assert (
        f"JobId={job_id} steps drained" in out
    ), f"expected the whole-set drain line after the job is cancelled: {out!r}"


@pytest.mark.slow
def test_follow_reports_het_step_components():
    """swait --follow (ALL mode) streams a local heterogeneous step's
    components as separate notifications, distinguished by
    step_het_component; only --json/--yaml expose this."""

    # Staggered so component 0 ends well before the drain component 1 triggers.
    # Component 0 gets STEP_SECS: this test must also poll scontrol for both
    # components before subscribing, so it needs at least the headroom the
    # single-step tests give, or the subscribe can land after component 0 ends.
    job_id = atf.submit_job_sbatch(
        "-N2 --time=5:00 --job-name=test_162_7_het_follow "
        f"--wrap 'srun -n1 sleep {STEP_SECS} : -n1 sleep {STEP_SECS * 3}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    # Bare suffix-less query: both components print identically here; a
    # "+0"/"+1" suffix query is unreliable due to pre-existing scontrol quirks.
    out = ""
    for _ in atf.timer(timeout=60, poll_interval=1, fatal=True):
        out = atf.run_command_output(f"scontrol -o show step {job_id}.0", quiet=True)
        if out.count("State=RUNNING") >= 2:
            break

    result = atf.run_command(
        f"swait --follow --json {job_id}",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 0, f"swait exited {result['exit_code']}"
    objs = [json.loads(line) for line in result["stdout"].splitlines() if line.strip()]
    comps = {
        atf.get_data_parser_number(obj["step_id"]["step_het_component"])
        for obj in objs
        if obj["step_id"]["step_id"] != "entire"
    }
    # Component 1 ends with the drain, so its line may be dropped (Issue
    # 50928); component 0 ends well before and must always be reported.
    assert 0 in comps, f"expected component 0 reported; got: {result['stdout']!r}"
    assert comps <= {
        0,
        1,
    }, f"unexpected het-step components reported; got: {result['stdout']!r}"


@pytest.mark.slow
def test_follow_timeout_exits_two():
    """--timeout bounds an ALL-mode (--follow) wait too, exiting 2.

    The DRAIN and STEP modes are covered elsewhere; --follow is the third
    dispatch mode and its timeout path was untested.
    """

    TIMEOUT_SECS = 5
    job_id = atf.submit_job_sbatch(
        "-N1 --time=10:00 --job-name=test_162_7_follow_timeout "
        f"--wrap 'srun -n1 sleep {STEP_SECS * 20}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=STEP_WAIT_SECS, fatal=True)
    result = atf.run_command(
        f"swait --follow --timeout={TIMEOUT_SECS} {job_id}",
        env_vars=f"SLURM_JOB_ID={job_id} SLURM_STEPMGR={atf.get_stepmgr_host(job_id)}",
        xfail=True,
        timeout=BLOCKING_CMD_SECS,
    )
    assert result["exit_code"] == 2, f"expected rc 2 for --timeout; got: {result!r}"
    assert (
        TIMEOUT_SECS - 1 <= result["duration"] < TIMEOUT_SECS + 8
    ), f"swait took {result['duration']:.1f}s; expected the deadline to fire"
