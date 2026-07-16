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


def test_usage_works():
    """--usage prints the short usage synopsis."""

    result = atf.run_command("swait --usage")
    assert result["exit_code"] == 0
    assert result["stdout"].startswith("Usage: swait [-hQvV]")


def test_version_works():
    """--version prints 'slurm <version>'."""

    result = atf.run_command("swait --version")
    assert result["exit_code"] == 0
    assert result["stdout"].startswith("slurm ")
    assert result["stdout"].split()[1][0].isdigit()


def test_invalid_timeout():
    """--timeout with a negative value is rejected at parse time."""

    result = atf.run_command("swait --timeout -1 12345", xfail=True)
    assert result["exit_code"] == 2
    assert "non-negative integer" in result["stderr"]


def test_step_suffix_rejected():
    """A jobid with a step suffix (jobid.0) is rejected at parse time."""

    result = atf.run_command("swait 42.0", xfail=True)
    assert result["exit_code"] == 2
    assert "swait operates on a job, not a step" in result["stderr"]


def test_array_range_rejected():
    """A jobid with an array task range (jobid_[range]) is rejected at parse time."""

    result = atf.run_command("swait 42_[0-3]", xfail=True)
    assert result["exit_code"] == 2
    assert "array-task ranges are not supported" in result["stderr"]


def test_het_offset_rejected():
    """A jobid with a het-job offset (jobid+1) is rejected at parse time."""

    result = atf.run_command("swait 42+1", xfail=True)
    assert result["exit_code"] == 2
    assert "het-job offsets are not supported" in result["stderr"]


def test_no_jobid_no_env():
    """With no positional and no jobid/sluid env vars, swait exits 2."""

    result = atf.run_command(
        "env -u SLURM_JOB_ID -u SLURM_JOB_SLUID -u SLURM_STEPMGR swait",
        xfail=True,
    )
    assert result["exit_code"] == 2
    assert "no job id given" in result["stderr"]


def test_nonexistent_jobid():
    """A bogus jobid produces 'no such job' on the first ctld lookup."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait {BOGUS_JOBID}", xfail=True)
    assert result["exit_code"] == 2
    assert "Invalid job id" in result["stderr"]


def test_nonexistent_array_task():
    """Array-task input <M>_<T> with an unknown master jobid surfaces the
    ctld's 'Invalid job id' error against the master jobid."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait {BOGUS_JOBID}_3", xfail=True)
    assert result["exit_code"] == 2
    assert "Invalid job id" in result["stderr"]
    assert f"JobId={BOGUS_JOBID}" in result["stderr"]


def test_env_var_fallback():
    """SLURM_JOB_ID is consulted when SLURM_JOB_SLUID is unset."""

    result = atf.run_command(
        f"env -u SLURM_STEPMGR -u SLURM_JOB_SLUID " f"SLURM_JOB_ID={BOGUS_JOBID} swait",
        xfail=True,
    )
    assert result["exit_code"] == 2
    # The bogus jobid must reach the ctld for the message to appear,
    # which proves the env-var fallback was consulted.
    assert "Invalid job id" in result["stderr"]


def test_invalid_sluid_rejected():
    """A SLUID that does not match the s<13chars> form is rejected at parse time."""

    result = atf.run_command("swait sZZZ", xfail=True)
    assert result["exit_code"] == 2
    # unfmt_job_id_string() returns ESLURM_INVALID_SLUID, which swait
    # surfaces as "cannot parse job id".
    assert "cannot parse" in result["stderr"]


def test_nonexistent_sluid():
    """A valid-form but unknown SLUID reaches the ctld and returns 'no such job'."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait {BOGUS_SLUID}", xfail=True)
    assert result["exit_code"] == 2
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
    assert result["exit_code"] == 2
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
    assert result["exit_code"] == 2
    # The label format diverges between the two paths: SLUID prints
    # "SLUID s...", numeric prints "job <N>". Confirm we took the SLUID
    # branch by checking the message format.
    assert "Invalid job id" in result["stderr"]
    assert "SLUID" in result["stderr"]
    assert f"job {BOGUS_JOBID}" not in result["stderr"]


def test_swait_timeout():
    """swait --timeout=N on a longer-running step exits 1."""

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
        result["exit_code"] == 1
    ), f"swait exited {result['exit_code']}, expected 1; stderr: {result['stderr']!r}"
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

    Uses .get() instead of atf.get_job_id_from_array_task() so non-array
    jobs left in scontrol's list by sibling tests in this module do not
    raise KeyError on the missing ArrayJobId field.
    """

    # Capture the observed id inside the poll callback so a final
    # re-call after the loop succeeds is unnecessary; without the
    # capture, a task that exits between the last successful poll and
    # the second lookup would silently return 0.
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
    assert result["exit_code"] == 2
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
    assert result["exit_code"] == 2
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
    assert result["exit_code"] == 2
    assert "not an array job" in result["stderr"]


def test_quiet_preserves_errors():
    """-Q lowers log verbosity but does not silence error-level
    messages; the exit code is preserved."""

    result = atf.run_command(f"env -u SLURM_STEPMGR swait -Q {BOGUS_JOBID}", xfail=True)
    assert result["exit_code"] == 2
    assert "Invalid job id" in result["stderr"]


def test_unknown_option_rejected():
    """An unknown long option is rejected with a 'Try --help' hint."""

    result = atf.run_command("swait --not-a-real-option", xfail=True)
    assert result["exit_code"] == 2
    assert "swait --help" in result["stderr"]


def test_too_many_positional_args():
    """Two positional job ids are rejected."""

    result = atf.run_command("swait 1 2", xfail=True)
    assert result["exit_code"] == 2
    assert "too many positional arguments" in result["stderr"]


def test_timeout_empty_value():
    """--timeout= with an empty value is rejected at parse time."""

    result = atf.run_command("swait --timeout= 12345", xfail=True)
    assert result["exit_code"] == 2
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
    """swait against a held (pending) job exits 2 with 'is still pending'."""

    job_id = atf.submit_job_sbatch(
        '-H -N1 --time=5:00 --job-name=test_pending_job --wrap "true"',
        fatal=True,
    )
    result = atf.run_command(f"env -u SLURM_STEPMGR swait {job_id}", xfail=True)
    assert result["exit_code"] == 2
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
