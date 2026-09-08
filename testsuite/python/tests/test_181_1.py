############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""RequeueExit and RequeueExitHold match an exit status or a Slurm return code.

A job ends carrying two values: the wait(2) status its own processes exited
with, and a Slurm error code when Slurm itself failed the job. These options
match against whichever of the two applies -- the return code if the job has
one, otherwise the exit status -- so a single list can name both
(_set_job_requeue_exit_value(), src/slurmctld/job_mgr.c).

Test mechanics worth knowing:
  - Jobs are submitted with --requeue. Without it the matcher returns before
    looking at anything, so every test here would pass vacuously.
  - Jobs that must requeue and then finish key off SLURM_RESTART_COUNT so the
    second run succeeds; a job that keeps failing keeps requeueing.
  - The Slurm-failure cases use a working directory that cannot be used. The
    stepd fails while opening the job's output file. A failing Prolog is not
    usable here: slurmctld requeues and holds a launch failure whether or not
    a rule matches, so the rule would prove nothing.
  - A requeueing job passes through FAILED before it returns to PENDING, and
    wait_for_job_state() gives up as soon as it sees a final state. The
    restart count, or the settled state, is polled instead.
  - The three exit-status tests are deliberately not version gated. Matching
    on an exit status, on a range, and the held variant all work before 26.11,
    so they are regression coverage for behaviour that should hold on every
    supported version. Only the return-code tests need 26.11.
"""

import os

import pytest

import atf

# Every Slurm error code is 1000 or greater; every exit status is below 256.
LOWEST_SLURM_ERROR_CODE = 1000

# Waiting for a requeued job to run a second time has to outlast the delay
# before it becomes eligible again. setup() asks for requeue_delay=0, but
# SchedulerParameters gained that option in 25.11, so on older versions the
# job still waits out AuthInfo=cred_expire, 120 seconds by default. This is
# only ever an upper bound: where requeue_delay=0 is honored the job reruns
# within seconds.
RERUN_TIMEOUT = 180

# A working directory the stepd cannot open a job's output file in, which is
# how these tests make Slurm fail a job before its script ever runs. Two
# properties are load-bearing:
#   - The job must use the DEFAULT output file. expand_stdio_fields() joins
#     this directory onto slurm-%j.out when the step record is built, so the
#     stepd opens an absolute path under it. An explicit --output would
#     disarm the primitive and the job would simply run.
#   - The directory must be one the stepd cannot CREATE, not merely one that
#     is absent: on ENOENT it calls mkdirpath() and retries the open, so a
#     missing directory under a writable parent is created and the job runs.
#     Nesting it under /dev/null gives ENOTDIR instead, on every node and
#     whichever user the job runs as.
UNUSABLE_WORK_DIR = "/dev/null/slurm_test_workdir"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # No accounting requirement: everything here is read through scontrol,
    # so the tests do not need a database.
    #
    # A requeued job is not eligible to run again until requeue_delay has
    # passed, and that defaults to AuthInfo=cred_expire, i.e. 120 seconds.
    # The tests that let a requeued job rerun would otherwise sit waiting for
    # two minutes each.
    #
    # SchedulerParameters gained requeue_delay in 25.11. On older versions it
    # is ignored and the full delay applies, which is why the waits that span
    # a rerun use RERUN_TIMEOUT rather than the default.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    # Without this the rerun waits for the next scheduling cycle, which is a
    # minute by default.
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


@pytest.fixture
def requeue_policy():
    """Set RequeueExit/RequeueExitHold for one test, restoring both afterwards.

    Both are always written, so a value left by one test cannot leak into the
    next. init_requeue_policy() re-reads them on reconfigure, which is what
    set_config_parameter triggers, so no restart is needed.
    """
    # live=False parses slurm.conf rather than `scontrol show config`. The
    # live view renders an unset RequeueExit as the string "(null)", which
    # would then be written back into slurm.conf on teardown and rejected by
    # the parser on every later reconfigure. The file view simply omits an
    # absent parameter, so default=None is reached and the teardown unsets it.
    names = ("RequeueExit", "RequeueExitHold")
    saved = {
        name: atf.get_config_parameter(name, default=None, live=False) for name in names
    }

    def apply(requeue_exit=None, requeue_exit_hold=None):
        atf.set_config_parameter("RequeueExit", requeue_exit)
        atf.set_config_parameter("RequeueExitHold", requeue_exit_hold)

    yield apply

    for name, value in saved.items():
        atf.set_config_parameter(name, value)


def _fail_once_script(name, exit_status):
    """Return a job script that exits exit_status once, then succeeds.

    A matching rule requeues the job every time it fails, so the body has to
    stop failing or the job never finishes. SLURM_RESTART_COUNT is what
    distinguishes the runs: a flag file would have to be visible from
    whichever node the requeued run lands on, and this needs no filesystem
    at all.
    """
    script = os.path.abspath(f"job_{name}.sh")
    atf.make_bash_script(
        script,
        f"""if [ "${{SLURM_RESTART_COUNT:-0}}" -gt 0 ]; then
    exit 0
fi
exit {exit_status}
""",
    )
    return script


def _restarts(job_id):
    # quiet=True because these are polled in a loop; default= covers a job
    # whose Restarts is not yet set, not a job that has left the queue.
    return int(atf.get_job_parameter(job_id, "Restarts", default=0, quiet=True))


def _state(job_id):
    """Return the job's state, or NOT_FOUND once it has left scontrol.

    get_job_parameter()'s default= only covers a missing parameter; it
    pytest.fail()s when the job itself is gone. These states are read from
    assert messages, which evaluate only on failure - exactly when the job's
    presence is least certain - so relying on it would replace a real
    diagnostic with "job not found".
    """
    jobs = atf.get_jobs(quiet=True)
    if int(job_id) not in jobs:
        return "NOT_FOUND"
    return jobs[int(job_id)].get("JobState", "NOT_FOUND")


def _wait_for_requeue(job_id):
    """Wait until the job has been requeued at least once.

    The timer is deliberately not fatal: returning False lets each caller
    fail with a message about what it was waiting for, which is more useful
    than a generic timeout.
    """
    for _ in atf.timer():
        if _restarts(job_id) >= 1:
            return True
    return False


def _wait_for_state(job_id, state, timeout=atf.default_polling_timeout):
    """Poll for a settled job state, tolerating the states passed through.

    timeout is only raised for waits that span a job rerun; see RERUN_TIMEOUT.
    """
    for _ in atf.timer(timeout=timeout):
        if _state(job_id) == state:
            return True
    return False


def _submit_failed_by_slurm():
    """Submit a job Slurm fails before it runs, and return the job id."""
    return atf.submit_job_sbatch(
        f"--requeue --chdir={UNUSABLE_WORK_DIR} --wrap 'exit 0'", fatal=True
    )


def _discover_slurm_error_code(requeue_policy):
    """Run one job Slurm fails with no rule set, and return the SlurmRC it reported.

    This also pins the premise every return-code test below depends on: the
    job carries a Slurm error code AND an exit status of 1 at the same time.
    If it ever reported 0:0 instead, a test asserting that RequeueExit=1 does
    not match it would pass for the wrong reason.
    """
    requeue_policy()
    probe = _submit_failed_by_slurm()
    atf.wait_for_job_state(probe, "DONE", fatal=True)
    rc = int(atf.get_job_parameter(probe, "SlurmRC", fatal=True))
    assert (
        rc >= LOWEST_SLURM_ERROR_CODE
    ), f"A job Slurm failed should carry a Slurm error code, got SlurmRC={rc}"
    exit_code = atf.get_job_parameter(probe, "ExitCode", fatal=True)
    assert exit_code == "1:0", (
        f"A job Slurm failed should report ExitCode 1:0 alongside SlurmRC={rc}; "
        f"got {exit_code}. The return-code tests below rely on both halves "
        "being set at once."
    )
    return rc


# Gated on scontrol rather than slurmctld: these tests read the job's SlurmRC
# through scontrol, and a client may be older than the controller under
# Slurm's supported version ordering (slurmdbd >= slurmctld >= slurmd >=
# clients). A new enough scontrol therefore implies a new enough slurmctld,
# but not the other way round.
requires_rc_matching = pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Ticket 24195: RequeueExit matched only exit statuses, and only "
    "up to 255, before 26.11",
)


# ---------------------------------------------------------------------------
# Matching on the job's own exit status
# ---------------------------------------------------------------------------


def test_requeue_on_exit_status(requeue_policy):
    """Verify a job whose exit status is listed is requeued and rerun"""

    requeue_policy(requeue_exit="3")

    job_id = atf.submit_job_sbatch(
        f"--requeue {_fail_once_script('exit3', 3)}", fatal=True
    )

    assert _wait_for_requeue(
        job_id
    ), "A job exiting 3 under RequeueExit=3 should be requeued"
    assert _wait_for_state(job_id, "COMPLETED", timeout=RERUN_TIMEOUT), (
        "The requeued run succeeds, so the job should end COMPLETED; it is in "
        f"state {_state(job_id)}"
    )
    assert _restarts(job_id) == 1, "The job should have been requeued exactly once"


def test_requeue_range_matches_only_listed_values(requeue_policy):
    """Verify a numeric range requeues inside it and not outside it"""

    requeue_policy(requeue_exit="1-9")

    inside = atf.submit_job_sbatch(
        f"--requeue {_fail_once_script('range_in', 3)}", fatal=True
    )
    assert _wait_for_requeue(inside), "Exit status 3 is inside 1-9 and should requeue"
    assert _wait_for_state(
        inside, "COMPLETED", timeout=RERUN_TIMEOUT
    ), f"The requeued run should succeed; job is {_state(inside)}"

    outside = atf.submit_job_sbatch("--requeue --wrap 'exit 20'", fatal=True)
    atf.wait_for_job_state(outside, "DONE", fatal=True)
    assert (
        _state(outside) == "FAILED"
    ), "Exit status 20 is outside 1-9 and should fail terminally"
    assert _restarts(outside) == 0, "Exit status 20 is outside 1-9 and must not requeue"


def test_requeue_exit_hold_on_exit_status(requeue_policy):
    """Verify RequeueExitHold requeues the job and leaves it held"""

    requeue_policy(requeue_exit_hold="3")

    # No _fail_once_script: the job is held rather than rerun, so it cannot
    # loop, and a plain `exit 3` is enough.
    job_id = atf.submit_job_sbatch("--requeue --wrap 'exit 3'", fatal=True)

    assert _wait_for_state(job_id, "SPECIAL_EXIT"), (
        "A job exiting 3 under RequeueExitHold=3 should land in SPECIAL_EXIT; "
        f"it is in state {_state(job_id)}"
    )
    assert _restarts(job_id) == 1, "A held requeue still counts as a restart"
    assert (
        int(atf.get_job_parameter(job_id, "Priority", fatal=True)) == 0
    ), "A job held by RequeueExitHold should have priority 0"


# ---------------------------------------------------------------------------
# Matching on a Slurm return code -- the case that needs a value above 255
# ---------------------------------------------------------------------------


@requires_rc_matching
def test_slurm_failure_does_not_requeue_without_a_rule(requeue_policy):
    """Verify a job Slurm failed fails terminally when no rule names its code

    This is the counterfactual for the tests below. If a Slurm-failed job
    requeued on its own, a rule-driven test could not tell whether the rule
    did anything.
    """

    requeue_policy()

    job_id = _submit_failed_by_slurm()
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    assert (
        _state(job_id) == "FAILED"
    ), "A job Slurm failed should fail terminally when no rule matches"

    assert (
        _restarts(job_id) == 0
    ), "No RequeueExit rule is set, so nothing should requeue this job"

    rc = int(atf.get_job_parameter(job_id, "SlurmRC", fatal=True))
    assert (
        rc >= LOWEST_SLURM_ERROR_CODE
    ), f"A job Slurm failed should carry a Slurm error code, got SlurmRC={rc}"


@requires_rc_matching
def test_requeue_on_slurm_return_code(requeue_policy):
    """Verify RequeueExit matches a Slurm return code above 255"""

    # Learn the error code from a job rather than hardcoding one, so the test
    # does not break if this failure starts reporting a different code.
    rc = _discover_slurm_error_code(requeue_policy)
    assert rc > 255, (
        "This test exists to prove values above 255 can be matched; the "
        f"failure reported SlurmRC={rc}, which is inside the old range"
    )

    requeue_policy(requeue_exit=str(rc))

    job_id = _submit_failed_by_slurm()
    # The working directory is still unusable, so every run fails and the
    # job would requeue forever. Watch for the first requeue, then stop it.
    requeued = _wait_for_requeue(job_id)
    atf.run_command(f"scancel {job_id}", fatal=True)

    assert requeued, (
        f"A job Slurm failed with SlurmRC={rc} should be requeued by "
        f"RequeueExit={rc}. Not requeueing means the return code is not being "
        "matched, or the value is being rejected for exceeding 255."
    )


@requires_rc_matching
def test_return_code_excludes_the_exit_status(requeue_policy):
    """Verify a job with a return code is NOT matched on its exit status

    slurm.conf.5 states the match is a strict precedence: a job with a
    non-zero return code is matched against that code, OTHERWISE against its
    exit status. The tests above prove both positive directions; this proves
    the exclusion, which a matcher written as "return code in the list OR
    exit status in the list" would fail while passing every other test here.

    A job Slurm failed carries SlurmRC >= 1000 and ExitCode=1:0 at the same time,
    so RequeueExit=1 names its exit status and not its return code. It must
    not requeue.
    """

    rc = _discover_slurm_error_code(requeue_policy)
    requeue_policy(requeue_exit="1")

    job_id = _submit_failed_by_slurm()
    # If the precedence is broken this job matches on its exit status and
    # requeues forever, so the wait has to be the bounded one and the job has
    # to be cancelled either way. Capture everything before the scancel.
    settled = _wait_for_state(job_id, "FAILED")
    restarts = _restarts(job_id)
    state = _state(job_id)
    exit_code = atf.get_job_parameter(job_id, "ExitCode", default="", quiet=True)
    atf.run_command(f"scancel {job_id}", fatal=True)

    assert settled, (
        f"A job Slurm failed with SlurmRC={rc} should fail terminally under "
        f"RequeueExit=1, which names its exit status and not its return "
        f"code; it is in state {state} with {restarts} restart(s)"
    )
    assert restarts == 0, (
        f"RequeueExit=1 requeued a job whose SlurmRC is {rc} and whose ExitCode "
        f"is {exit_code}. Matching it means the exit status is being "
        "consulted for a job that has a return code, so the documented "
        "precedence is not being applied."
    )


@requires_rc_matching
@pytest.mark.skipif(
    atf.get_version("bin/squeue") < (26, 11),
    reason="Ticket 24195: the squeue SlurmRC format field was added in 26.11",
)
def test_requeue_exit_hold_on_slurm_return_code(requeue_policy):
    """Verify RequeueExitHold matches a Slurm return code and holds the job"""

    rc = _discover_slurm_error_code(requeue_policy)
    requeue_policy(requeue_exit_hold=str(rc))

    # Being held stops the job re-running, so this one cannot loop.
    job_id = _submit_failed_by_slurm()

    assert _wait_for_state(job_id, "SPECIAL_EXIT"), (
        f"A job Slurm failed with SlurmRC={rc} should land in SPECIAL_EXIT under "
        f"RequeueExitHold={rc}; it is in state {_state(job_id)}"
    )
    assert (
        int(atf.get_job_parameter(job_id, "Priority", fatal=True)) == 0
    ), "A job held by RequeueExitHold should have priority 0"

    # A held job stays pending and plainly visible to squeue, so this can
    # show squeue rendering a NON-ZERO rc. test_114_7 pins that the field is
    # registered and wired; here it can be told apart from a literal 0.
    squeue_rc = atf.run_command_output(
        f"squeue --noheader -O SlurmRC -j {job_id}", fatal=True
    ).strip()
    assert squeue_rc == str(rc), (
        f"squeue -O SlurmRC should render the job's own return code {rc}; got "
        f"{squeue_rc!r}. A value of 0 means the field renders a constant "
        "rather than reading job_exit_rc.rc."
    )


@requires_rc_matching
def test_one_list_holds_both_value_spaces(requeue_policy):
    """Verify an exit status and a return code can share one RequeueExit list

    The two are told apart by whether the job has a return code, not by the
    value, so both entries have to work from the same list.
    """

    rc = _discover_slurm_error_code(requeue_policy)
    requeue_policy(requeue_exit=f"3,{rc}")

    by_status = atf.submit_job_sbatch(
        f"--requeue {_fail_once_script('mixed', 3)}", fatal=True
    )
    assert _wait_for_requeue(by_status), "The exit-status entry should still match"
    assert _wait_for_state(
        by_status, "COMPLETED", timeout=RERUN_TIMEOUT
    ), f"The requeued run should succeed; job is {_state(by_status)}"

    by_rc = _submit_failed_by_slurm()
    matched = _wait_for_requeue(by_rc)
    atf.run_command(f"scancel {by_rc}", fatal=True)
    assert matched, "The return-code entry should match from the same list"


@requires_rc_matching
def test_requeue_exit_accepts_the_documented_maximum(requeue_policy):
    """Verify RequeueExit and RequeueExitHold accept 65535

    slurm.conf.5 documents values up to 65535, well past the 255 an exit
    status can reach, so the whole return-code range can be listed.
    """

    requeue_policy(requeue_exit="65535", requeue_exit_hold="65535")

    for name in ("RequeueExit", "RequeueExitHold"):
        value = atf.get_config_parameter(name, live=True)
        assert value == "65535", f"{name}=65535 should be accepted; got {value!r}"
