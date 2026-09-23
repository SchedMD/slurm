############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
#
# Verify swait behavior against a stepmgr-enabled cluster.
############################################################################
import time

import pytest

import atf

BOGUS_JOBID = 4294967292
BOGUS_SLUID = "s0000000000001"

# 26.11 renumbered swait's exit codes: an error moved 2 -> 1 and --timeout
# expiry 1 -> 2. These tests run against clients of both vintages, so name
# the two codes rather than asserting a bare number. Tests already gated on
# bin/swait keep the literal their gate pins.
RC_ERROR, RC_TIMEOUT = (1, 2) if atf.get_version("bin/swait") >= (26, 11) else (2, 1)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        reason="swait was added in 26.05",
    )
    atf.require_version(
        (26, 5),
        component="sbin/slurmd",
        reason="swait talks directly to the 26.05 stepmgr stepd",
    )
    atf.require_tool("swait")
    atf.require_nodes(1)
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def test_help_works():
    """--help prints the full help text."""

    result = atf.run_command("swait --help")
    assert result["exit_code"] == 0
    out = result["stdout"]
    assert out.startswith("Usage: swait ")
    assert "Options:" in out
    assert "--timeout=SECS" in out
    assert "-Q, --quiet" in out
    assert "-h, --help" in out
    assert "Exit status:" in out


def test_help_lists_26_11_options():
    """--help advertises the 26.11 options. The help text is the first place
    a user looks, so an option silently dropped from it must fail."""

    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: --follow/--json/--yaml were added in 26.11",
    )
    out = atf.run_command_output("swait --help", fatal=True)
    for opt in ("--follow", "--json[=data_parser]", "--yaml[=data_parser]"):
        assert opt in out, f"--help does not advertise {opt}; got: {out!r}"
    assert (
        "--json=list" in out
    ), f"--help does not mention the =list plugin listing; got: {out!r}"


def test_usage_works():
    """--usage prints the short usage synopsis."""

    result = atf.run_command("swait --usage")
    assert result["exit_code"] == 0
    assert result["stdout"].startswith("Usage: swait [-hQvV]")


def test_usage_shows_step_target():
    """--usage shows the optional .<stepid> suffix in the target grammar."""

    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: the <jobid>.<stepid> target was added in 26.11",
    )
    out = atf.run_command_output("swait --usage", fatal=True)
    assert (
        ".<stepid>" in out
    ), f"--usage does not show the .<stepid> target suffix; got: {out!r}"


def test_version_works():
    """--version prints 'slurm <version>'."""

    result = atf.run_command("swait --version")
    assert result["exit_code"] == 0
    assert result["stdout"].startswith("slurm ")
    assert result["stdout"].split()[1][0].isdigit()


def test_invalid_timeout():
    """--timeout with a negative value is rejected at parse time."""

    result = atf.run_command("swait --timeout -1 12345", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "non-negative integer" in result["stderr"]


def test_step_suffix_rejected():
    """A jobid with a step suffix (jobid.0) is rejected at parse time (pre-26.11)."""

    atf.require_version(
        (26, 5),
        component="bin/swait",
        max_version=(26, 11),
        reason="Issue 50928: the step suffix is rejected at parse time before 26.11",
    )
    result = atf.run_command("swait 42.0", xfail=True)
    # The gate above pins this to a pre-26.11 swait, which still numbers a
    # parse error 2; the swap to 1 only lands in 26.11.
    assert result["exit_code"] == 2
    assert "swait operates on a job, not a step" in result["stderr"]


def test_step_suffix_accepted():
    """A jobid with a step suffix (jobid.0) selects a single-step wait (26.11+)."""

    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: the step suffix selects a single-step wait in 26.11",
    )
    result = atf.run_command(f"env -u SLURM_STEPMGR swait {BOGUS_JOBID}.0", xfail=True)
    assert result["exit_code"] == 1, f"expected rc 1; got: {result!r}"
    # The suffix is accepted at parse time, so swait gets far enough to fail
    # on the bogus job id rather than rejecting the target itself.
    assert (
        "operates on a job, not a step" not in result["stderr"]
    ), f"stale pre-26.11 rejection message: {result['stderr']!r}"
    # Positive marker: naming the job id proves the target parsed and swait
    # reached the controller lookup, rather than failing for some other reason.
    assert (
        f"JobId={BOGUS_JOBID}" in result["stderr"]
    ), f"expected the ctld job lookup to be reached; got: {result['stderr']!r}"


def test_array_range_rejected():
    """A jobid with an array task range (jobid_[range]) is rejected at parse time."""

    result = atf.run_command("swait 42_[0-3]", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "array-task ranges are not supported" in result["stderr"]


def test_array_task_list_rejected():
    """swait(1) DESCRIPTION lists task lists alongside task ranges as
    unsupported. A comma list never reaches the array-bitmap check, so it
    is rejected earlier, as an unparsable job id -- but still as an error."""

    result = atf.run_command("swait 42_1,3", xfail=True)
    assert (
        result["exit_code"] == RC_ERROR
    ), f"swait exited {result['exit_code']}, expected {RC_ERROR} for a task list"
    assert (
        "cannot parse job id" in result["stderr"]
    ), f"unexpected stderr: {result['stderr']!r}"


def test_het_offset_rejected():
    """A jobid with a het-job offset (jobid+1) is rejected at parse time."""

    result = atf.run_command("swait 42+1", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "het-job offsets are not supported" in result["stderr"]


def test_het_step_rejected():
    """A jobid.stepid with a het-step-component suffix (jobid.0+1) is
    rejected at parse time, once the step suffix itself is parsed (26.11+).
    """

    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: the het-step-component suffix is only reachable once "
        "the step suffix itself is parsed, in 26.11",
    )
    result = atf.run_command("swait 42.0+1", xfail=True)
    assert result["exit_code"] == 1, f"expected rc 1; got: {result!r}"
    assert (
        "het steps are not supported" in result["stderr"]
    ), f"unexpected stderr: {result['stderr']!r}"


@pytest.mark.parametrize("suffix", ["batch", "extern", "interactive"])
def test_special_step_rejected(suffix):
    """A jobid.stepid target naming a special step (batch/extern/
    interactive) is rejected at parse time (26.11+).
    """

    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: the step suffix selects a single-step wait in 26.11",
    )
    result = atf.run_command(f"swait 42.{suffix}", xfail=True)
    assert result["exit_code"] == 1, f"suffix={suffix}: {result!r}"
    assert (
        "cannot wait on a special step" in result["stderr"]
    ), f"suffix={suffix}: {result['stderr']!r}"


# The numeric sentinels the special-step names resolve to:
# SLURM_INTERACTIVE_STEP, SLURM_BATCH_SCRIPT, SLURM_EXTERN_CONT and
# SLURM_PENDING_STEP.
@pytest.mark.parametrize("step_id", [4294967290, 4294967291, 4294967292, 4294967293])
def test_numeric_special_step_rejected(step_id):
    """A jobid.stepid target giving a special step by its numeric id is
    rejected too, but earlier than the named form: the id is above the
    largest ordinary step id, so the target never parses (26.11+).
    """

    atf.require_version(
        (26, 11),
        component="bin/swait",
        reason="Issue 50928: the step suffix selects a single-step wait in 26.11",
    )
    result = atf.run_command(f"swait 42.{step_id}", xfail=True)
    assert result["exit_code"] == 1, f"step_id={step_id}: {result!r}"
    assert (
        "cannot parse job id" in result["stderr"]
    ), f"step_id={step_id}: {result['stderr']!r}"


def test_no_jobid_no_env():
    """With no positional and no jobid/sluid env vars, swait exits RC_ERROR."""

    result = atf.run_command(
        "env -u SLURM_JOB_ID -u SLURM_JOB_SLUID -u SLURM_STEPMGR swait",
        xfail=True,
    )
    assert result["exit_code"] == RC_ERROR
    assert "no job id given" in result["stderr"]


def test_nonexistent_jobid():
    """A bogus jobid produces 'no such job' on the first ctld lookup."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait {BOGUS_JOBID}", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "Invalid job id" in result["stderr"]


def test_nonexistent_array_task():
    """Array-task input <M>_<T> with an unknown master jobid surfaces the
    ctld's 'Invalid job id' error against the master jobid."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait {BOGUS_JOBID}_3", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "Invalid job id" in result["stderr"]
    assert f"JobId={BOGUS_JOBID}" in result["stderr"]


def test_env_var_fallback():
    """SLURM_JOB_ID is consulted when SLURM_JOB_SLUID is unset."""

    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_SLUID " f"SLURM_JOB_ID={BOGUS_JOBID} swait",
        xfail=True,
    )
    assert result["exit_code"] == RC_ERROR
    # The bogus jobid must reach the ctld for the message to appear,
    # which proves the env-var fallback was consulted.
    assert "Invalid job id" in result["stderr"]


def test_invalid_sluid_rejected():
    """A SLUID that does not match the s<13chars> form is rejected at parse time."""

    result = atf.run_command("swait sZZZ", xfail=True)
    assert result["exit_code"] == RC_ERROR
    # unfmt_job_id_string() returns ESLURM_INVALID_SLUID, which swait
    # surfaces as "cannot parse job id".
    assert "cannot parse" in result["stderr"]


def test_nonexistent_sluid():
    """A valid-form but unknown SLUID reaches the ctld and returns 'no such job'."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait {BOGUS_SLUID}", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "Invalid job id" in result["stderr"]
    # The label helper prints SLUID identifiers as "SLUID s..." rather
    # than "job N"; confirm we did not fall through to the numeric path.
    assert "SLUID" in result["stderr"]


def test_sluid_env_var_fallback():
    """SLURM_JOB_SLUID is consulted when the numeric env vars are unset."""

    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_ID " f"SLURM_JOB_SLUID={BOGUS_SLUID} swait",
        xfail=True,
    )
    assert result["exit_code"] == RC_ERROR
    # Reaching "Invalid job id" proves the SLUID env var was consulted and
    # forwarded to slurmctld; a numeric-only env-var chain would have
    # bailed out with "no job id given" instead.
    assert "Invalid job id" in result["stderr"]


def test_sluid_env_var_wins_over_numeric():
    """When both SLURM_JOB_SLUID and SLURM_JOB_ID are set, the SLUID form wins."""

    result = atf.run_command(
        f"env -u SLURM_STEPMGR "
        f"SLURM_JOB_SLUID={BOGUS_SLUID} SLURM_JOB_ID={BOGUS_JOBID} swait",
        xfail=True,
    )
    assert result["exit_code"] == RC_ERROR
    # The label format diverges between the two paths: SLUID prints
    # "SLUID s...", numeric prints "job <N>". Confirm we took the SLUID
    # branch by checking the message format.
    assert "Invalid job id" in result["stderr"]
    assert "SLUID" in result["stderr"]
    assert f"job {BOGUS_JOBID}" not in result["stderr"]


def test_swait_timeout():
    """swait --timeout=N on a longer-running step exits RC_TIMEOUT."""

    TIMEOUT_SECS = 3
    job_id = atf.submit_job_sbatch(
        f"-N1 --time=5:00 --job-name=test_swait_timeout "
        f"--output={atf.module_tmp_path}/slurm-%j.out "
        f"--wrap 'srun -n1 sleep 10'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=30, fatal=True)
    start = time.monotonic()
    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID "
        f"swait --timeout {TIMEOUT_SECS} {job_id}",
        timeout=TIMEOUT_SECS + 30,
        xfail=True,
    )
    elapsed = time.monotonic() - start
    assert (
        result["exit_code"] == RC_TIMEOUT
    ), f"swait exited {result['exit_code']}, expected {RC_TIMEOUT}; stderr: {result['stderr']!r}"
    # Bound elapsed in both directions so a granularity regression --
    # firing too early or overshooting -- is caught.
    assert TIMEOUT_SECS - 1 <= elapsed < TIMEOUT_SECS + 5, (
        f"swait --timeout={TIMEOUT_SECS} returned in {elapsed:.1f}s, "
        f"expected ~{TIMEOUT_SECS}s"
    )


def _resolve_array_task_id(master_id, task_offset, timeout=60):
    """Poll until the per-task assigned job id for master_id_task_offset
    becomes observable in squeue.

    wait_for_job_state(master_id, ...) cannot be used as a sync point
    for array work: when tasks dispatch sequentially (a 1-node testbed
    with -N1 tasks), the master id stays on a pending placeholder
    until the LAST task starts, by which point the earlier tasks may
    have already exited. Waiting on a specific per-task id avoids that
    pitfall.
    """

    # Poll every second rather than atf.timer()'s default of timeout/10.
    # The task being resolved is short-lived, so a coarse interval can
    # spend a caller's whole window here and hand back an id whose step
    # has already drained.
    job_id = 0
    for _ in atf.timer(timeout=timeout, poll_interval=1, quiet=True, fatal=True):
        job_id = atf.get_job_id_from_array_task(master_id, task_offset)
        if job_id:
            break
    return job_id


def test_array_task_out_of_range():
    """Asking for a task offset outside the submitted array surfaces a
    specific 'task offset not found in array' error from the discovery
    walk (not a generic 'no such job' from the master lookup).
    """

    # Submit a 2-task array so the master id is valid and slurmctld
    # returns the per-task records. We do not need any task to be
    # RUNNING -- the discovery walk in _resolve_stepmgr_via_ctld
    # operates on whatever records the controller knows about, so
    # waiting only for the controller to register the array is
    # enough. Resolving task 0's per-task id is the cleanest gate.
    job_id = atf.submit_job_sbatch(
        "--array=0-1 -N1 --time=5:00 --job-name=test_array_task_out_of_range "
        '--wrap "srun -n1 sleep 15"',
        fatal=True,
    )
    _resolve_array_task_id(job_id, 0)
    result = atf.run_command(f"swait {job_id}_99", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "not found" in result["stderr"]
    assert f"array task {job_id}_99" in result["stderr"]


def test_array_job_no_task_offset_rejected():
    """swait <master> on an array job (no _task offset) is rejected:
    with multiple per-task records, swait cannot pick one stepmgr.
    """

    job_id = atf.submit_job_sbatch(
        "--array=0-1 -N1 --time=5:00 --job-name=test_array_no_task "
        '--wrap "srun -n1 sleep 15"',
        fatal=True,
    )
    _resolve_array_task_id(job_id, 0)
    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID " f"swait {job_id}",
        xfail=True,
    )
    assert result["exit_code"] == RC_ERROR
    assert "pass a specific task offset" in result["stderr"]


def test_nonarray_job_with_task_offset_rejected():
    """swait <jobid>_<task> on a non-array job hits the 'not an array
    job' branch of _resolve_stepmgr_via_ctld.
    """

    job_id = atf.submit_job_sbatch(
        "-N1 --time=5:00 --job-name=test_nonarray_with_offset "
        '--wrap "srun -n1 sleep 15"',
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=60, fatal=True)
    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID " f"swait {job_id}_0",
        xfail=True,
    )
    assert result["exit_code"] == RC_ERROR
    assert "not an array job" in result["stderr"]


def test_quiet_preserves_errors():
    """-Q lowers log verbosity but does not silence error-level
    messages; the exit code is preserved."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait -Q {BOGUS_JOBID}", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "Invalid job id" in result["stderr"]


def test_unknown_option_rejected():
    """An unknown long option is rejected with a 'Try --help' hint."""

    result = atf.run_command("swait --not-a-real-option", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "swait --help" in result["stderr"]


def test_too_many_positional_args():
    """Two positional job ids are rejected."""

    result = atf.run_command("swait 1 2", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "too many positional arguments" in result["stderr"]


@pytest.mark.parametrize("value", ["", "abc"])
def test_timeout_invalid_value(value):
    """swait(1): SECS "must be a non-negative integer". An empty or
    non-numeric value is rejected at parse time."""

    result = atf.run_command(f"swait --timeout={value} 12345", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "--timeout: invalid value" in result["stderr"]


def test_swait_live_drain_via_ctld():
    """swait <jobid> from outside the job forces a ctld stepmgr lookup
    and still drains cleanly.

    Inside sbatch, $SLURM_STEPMGR is set and swait takes the env fast
    path. Submitting the job, waiting for the step to register, and
    calling swait with the env stripped exercises
    _resolve_stepmgr_via_ctld() instead.
    """

    STEP_SECS = 5
    job_id = atf.submit_job_sbatch(
        f"-N1 --time=5:00 --job-name=test_swait_live_drain_via_ctld "
        f"--output={atf.module_tmp_path}/slurm-%j.out "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, timeout=60, fatal=True)
    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID "
        f"swait --timeout 30 {job_id}",
        timeout=60,
    )
    if result["exit_code"] != 0:
        out = atf.module_tmp_path / f"slurm-{job_id}.out"
        out_text = out.read_text() if out.exists() else "<no output>"
        pytest.fail(
            f"swait did not drain cleanly; rc={result['exit_code']}, "
            f"stderr={result['stderr']!r}; sbatch output:\n{out_text}"
        )
    assert result["stderr"] == "", f"unexpected stderr: {result['stderr']!r}"


def test_autocomplete():
    """--autocomplete suggests matching long options."""

    result = atf.run_command("swait --autocomplete=--t")
    assert result["exit_code"] == 0
    assert "--timeout=" in result["stdout"]


def test_pending_job_rejected():
    """swait against a held (pending) job exits 1 with 'is still pending'."""

    job_id = atf.submit_job_sbatch(
        '-H -N1 --time=5:00 --job-name=test_pending_job --wrap "true"',
        fatal=True,
    )
    result = atf.run_command(f"env -u SLURM_STEPMGR swait {job_id}", xfail=True)
    assert result["exit_code"] == RC_ERROR
    assert "is still pending" in result["stderr"]


def test_array_task_drain():
    """swait <master>_0 from outside an array job drains cleanly via the
    per-task ctld lookup.

    The input jobid is the array master, but slurm_load_job returns
    per-task records; the discovery walk matches on the requested task
    offset and rewrites target->job_id to the per-task id.
    """

    STEP_SECS = 5
    job_id = atf.submit_job_sbatch(
        f"--array=0-1 -N1 --time=5:00 --job-name=test_array_task_drain "
        f"--output={atf.module_tmp_path}/slurm-%j.out "
        f"--wrap 'srun -n1 sleep {STEP_SECS}'",
        fatal=True,
    )
    # scontrol shows array steps as StepId=<master>_<offset>.<step>;
    # pass that form so wait_for_step's regex matches.
    atf.wait_for_step(f"{job_id}_0", 0, timeout=120, fatal=True)
    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_ID -u SLURM_JOB_SLUID "
        f"swait {job_id}_0 --timeout 30",
        timeout=60,
    )
    assert (
        result["exit_code"] == 0
    ), f"swait did not drain cleanly; stderr: {result['stderr']}"
    assert result["stderr"] == "", f"unexpected stderr: {result['stderr']!r}"


def test_quiet_and_verbose_mutually_exclusive():
    """swait(1) documents -Q and -v as mutually exclusive on both entries."""

    result = atf.run_command("swait -Q -v 1", xfail=True)
    assert result["exit_code"] == RC_ERROR, f"expected rc {RC_ERROR}; got: {result!r}"
    assert (
        "exclusive" in result["stderr"]
    ), f"expected a mutual-exclusion error; got: {result['stderr']!r}"
