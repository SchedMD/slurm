############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that stopping and resuming a waiting srun does not end its wait.

Resuming a stopped srun delivers SIGCONT. That is not a request to give up, so
srun must keep waiting on the job allocation or the job step it asked for,
within any --immediate limit, and use it once it is available.
"""

import os
import re
import signal
import time
from pathlib import Path

import pexpect
import pytest

import atf

# How long to watch srun after SIGCONT. A srun that ends its wait on the
# signal does so at once, well within this.
WATCH_TIME = 5

# The --immediate limit for test_resume_keeps_immediate_limit, and how late
# srun may give up after it passes.
IMMEDIATE = 10
IMMEDIATE_SLACK = 5

# srun 26.11 and later queue a waiting step under a real StepId and report it
# (srun.1). An older srun gets its StepId only once the step launches.
STEP_ID_AT_SUBMIT = atf.get_version("bin/srun") >= (26, 11)

resubmits_on_sigcont = pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 5),
    reason="srun before 26.05 cancels a pending job on SIGCONT and submits"
    " another one",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    # test_resume_while_pending_step holds both CPUs of a 2-CPU job with one
    # step, so that a second step has to wait for them.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()


@pytest.fixture
def spawn_srun():
    """Return a function that spawns srun with the given arguments.

    Every srun it spawned is killed afterwards, even if the test fails midway.
    """
    children = []

    def _spawn(args):
        child = atf.run_command_pexpect(f"srun {args}")
        children.append(child)
        return child

    yield _spawn

    for child in children:
        child.close(force=True)


def _process_state(pid):
    """Return the state letter of pid from /proc, e.g. "S" or "T".

    pid is a srun that pexpect spawned here, so its /proc entry is local.
    """
    stat = Path(f"/proc/{pid}/stat").read_text()
    # The command name is in parentheses and may contain spaces.
    return stat.rsplit(")", 1)[1].split()[0]


def _stop_and_resume(pid):
    """Stop pid, and resume it once it has stopped."""
    os.kill(pid, signal.SIGSTOP)
    # A stop takes effect at once, so poll faster than the default interval.
    for _ in atf.timer(poll_interval=0.1, fatal=True):
        if _process_state(pid) == "T":
            break
    os.kill(pid, signal.SIGCONT)


def _expect_queued_job(child):
    """Wait for srun to report its job as queued, and return the job id."""
    assert (
        child.expect(
            [
                r"job (\d+) queued and waiting for resources",
                pexpect.EOF,
                pexpect.TIMEOUT,
            ],
            timeout=atf.default_command_timeout,
        )
        == 0
    ), f"srun should wait for its held job. Output:\n{child.before}"
    return int(child.match.group(1))


@resubmits_on_sigcont
def test_resume_while_pending_allocation(spawn_srun):
    """Test srun keeps its pending job when stopped and resumed.

    A srun that takes the signal for an interrupted wait cancels the pending
    job and submits a new one, and it can keep doing so without end.
    """
    child = spawn_srun("--hold true")
    job_id = _expect_queued_job(child)

    _stop_and_resume(child.pid)

    # srun prints this for every job it submits, so seeing it again means it
    # gave up the first job and submitted another.
    assert (
        child.expect(
            ["queued and waiting for resources", pexpect.EOF, pexpect.TIMEOUT],
            timeout=WATCH_TIME,
        )
        == 2
    ), (
        f"srun should keep waiting on job {job_id} after SIGCONT."
        f" Output:\n{child.before}"
    )
    assert (
        atf.get_job_parameter(job_id, "JobState") == "PENDING"
    ), f"job {job_id} should still be pending after srun is resumed"

    atf.run_command(f"scontrol release {job_id}", fatal=True)
    assert (
        child.expect(
            [
                rf"job {job_id} has been allocated resources",
                pexpect.EOF,
                pexpect.TIMEOUT,
            ],
            timeout=atf.default_command_timeout,
        )
        == 0
    ), (
        f"srun should run in job {job_id} once it is released."
        f" Output:\n{child.before}"
    )
    child.expect(pexpect.EOF, timeout=atf.default_command_timeout)
    child.close()
    assert child.exitstatus == 0, f"srun should succeed. Output:\n{child.before}"


@resubmits_on_sigcont
def test_resume_keeps_immediate_limit(spawn_srun):
    """Test srun keeps its --immediate limit when stopped and resumed.

    Every SIGCONT interrupts srun's wait. A srun that restarts the limit on each
    interruption does not give up while they keep coming, and one that drops
    the limit waits forever.
    """
    child = spawn_srun(f"--immediate={IMMEDIATE} --hold true")
    job_id = _expect_queued_job(child)
    waiting_since = time.time()

    _stop_and_resume(child.pid)
    # Keep interrupting the wait for half of the limit.
    for _ in atf.timer(timeout=IMMEDIATE / 2, poll_interval=0.5, xfail=True):
        os.kill(child.pid, signal.SIGCONT)

    assert child.isalive(), (
        f"srun should still be waiting within its {IMMEDIATE}s limit."
        f" Output:\n{child.before}"
    )
    assert (
        atf.get_job_parameter(job_id, "JobState") == "PENDING"
    ), f"job {job_id} should still be pending after srun is resumed"

    assert (
        child.expect(
            ["Unable to allocate resources", pexpect.EOF, pexpect.TIMEOUT],
            timeout=IMMEDIATE + IMMEDIATE_SLACK - (time.time() - waiting_since),
        )
        == 0
    ), (
        f"srun should give up once its {IMMEDIATE}s limit has passed."
        f" Output:\n{child.before}"
    )
    waited = time.time() - waiting_since
    assert (
        waited >= IMMEDIATE - 1
    ), f"srun should not give up before its {IMMEDIATE}s limit, gave up after {waited:.1f}s"
    child.expect(pexpect.EOF, timeout=atf.default_command_timeout)
    child.close()
    assert child.exitstatus != 0, f"srun should fail. Output:\n{child.before}"


def test_resume_while_pending_step(spawn_srun):
    """Test srun keeps waiting for step resources when stopped and resumed.

    A srun that ends the wait on the signal asks for the step again at once,
    and it keeps doing so, logging a retry each time.
    """
    script = Path("hog.sh")
    atf.make_bash_script(
        script, "srun --exclusive -n2 sleep infinity &\nsleep infinity\n"
    )
    job_id = atf.submit_job(
        "sbatch", "-N1 -n2 -t5", str(script), wrap_job=False, fatal=True
    )
    assert job_id != 0, "sbatch should submit the job"
    atf.wait_for_step(job_id, 0, fatal=True)

    child = spawn_srun(f"--jobid={job_id} -n1 -vv true")
    if STEP_ID_AT_SUBMIT:
        # srun.1: a waiting step is reported as "StepId=<jobid>.<stepid> queued".
        marker = r"StepId=(\d+\.\d+) queued"
    else:
        # -vv logs the step request, just before srun waits for the step.
        marker = "requesting job"
    assert (
        child.expect(
            [marker, pexpect.EOF, pexpect.TIMEOUT],
            timeout=atf.default_command_timeout,
        )
        == 0
    ), f"srun should wait for its step. Output:\n{child.before}"
    step_id = child.match.group(1) if STEP_ID_AT_SUBMIT else None

    _stop_and_resume(child.pid)

    assert (
        child.expect(["retrying", pexpect.EOF, pexpect.TIMEOUT], timeout=WATCH_TIME)
        == 2
    ), f"srun should keep waiting after SIGCONT. Output:\n{child.before}"
    if STEP_ID_AT_SUBMIT:
        step = atf.run_command_output(f"scontrol -o show step {step_id}", fatal=True)
        assert (
            "State=PENDING" in step
        ), f"step {step_id} should still be pending after srun is resumed: {step}"

    # Freeing the CPUs wakes the waiting srun, which then runs its step.
    atf.run_command(f"scancel {job_id}.0", fatal=True)
    if STEP_ID_AT_SUBMIT:
        # srun.1: the step keeps its StepId when it launches.
        assert (
            child.expect(
                [rf"StepId={re.escape(step_id)} started", pexpect.EOF, pexpect.TIMEOUT],
                timeout=atf.default_command_timeout,
            )
            == 0
        ), f"step {step_id} should start once the CPUs are free. Output:\n{child.before}"
    assert (
        child.expect(
            [pexpect.EOF, pexpect.TIMEOUT], timeout=atf.default_command_timeout
        )
        == 0
    ), f"srun should run its step once the CPUs are free. Output:\n{child.before}"
    child.close()
    assert child.exitstatus == 0, f"srun should succeed. Output:\n{child.before}"
