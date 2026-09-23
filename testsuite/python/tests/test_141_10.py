############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Without fast_power_up_launch a job still launches, on the periodic poll.

The option is opt-in, so by default a job waiting on nodes to power up is
launched by job_time_limit() in the slurmctld background thread, up to
PERIODIC_TIMEOUT after the last of its nodes registers. That path was
restructured into _test_job_config_complete() when the option was added, and
every power-up job on a default install depends on it, so it is worth pinning
in its own right rather than only testing the opt-in behavior.

Two things are asserted. The job must reach RUNNING within PERIODIC_TIMEOUT
plus slack, which is what proves the default path still launches it at all.
And the launch must be attributable to the poll: a "Testing job time limits"
line has to fall between the last node registering and the job's configuration
completing. The second is the mirror of what test_141_7 asserts, and is
deterministic for the same reason -- the poll either ran in that interval or
it did not, so nothing depends on when the test looks.
"""

import re
import time
from datetime import datetime

import pytest

import atf

pytestmark = pytest.mark.slow

node1 = "node1"
resume_timeout = 60
suspend_time = 60


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Runs slurmd on same machine as slurmctld")
    # This module is about the default, so the option must stay unset. It is
    # only ever added to the config by the modules that do test it.
    atf.require_config_parameter("SlurmctldParameters", None)
    # job_time_limit() announces itself at debug2; the poll cannot be
    # identified without it.
    # The poll announces itself at debug2; the list form means an environment
    # already more verbose than that is not downgraded.
    atf.require_config_parameter(
        "SlurmctldDebug", ["debug2", "debug3", "debug4", "debug5"]
    )
    # logged_times() parses the bracketed ISO timestamp.
    atf.require_config_parameter("LogTimeFormat", "iso8601_ms")
    # A PrologSlurmctld still running when the last node registers defers the
    # launch to prolog_running_decr(), which logs a different message than the
    # one verify_launched_by_poll() matches.
    atf.require_config_parameter("PrologSlurmctld", None)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)

    # A static cloud node, which comes up POWERED_DOWN. Its port differs from
    # the other 141_* cloud modules so the two can never collide.
    atf.require_config_parameter("NodeName", {node1: {"State": "CLOUD", "Port": 6823}})
    atf.require_config_parameter("PartitionName", {"cloud": {"Nodes": node1}})

    # The test starts the slurmd itself, so don't wait for the node to come up.
    atf.start_slurmctld(clean=True)

    # require_config_parameter_includes() is additive and the value survives in
    # the config ATF restores between modules, so an earlier module can leave
    # it set. That is an unusable environment, not a product failure, so say so
    # here rather than letting an assertion in the test report a regression.
    live = atf.get_config_parameter("SlurmctldParameters", live=True, quiet=True)
    if "fast_power_up_launch" in (live or ""):
        pytest.fail(
            "fast_power_up_launch is configured, so this module cannot test "
            "the default path"
        )

    yield

    # conftest only cancels jobs when it started Slurm itself (it keys off
    # properties["slurm-started"], which require_slurm_running() sets and
    # start_slurmctld() does not), so cancel them here. Otherwise the running
    # job's slurmstepd is orphaned by the shutdown below and outlives the test.
    atf.cancel_all_jobs(fatal=True)

    atf.stop_slurmctld(also_slurmds=True)


def logged_times(pattern, since=None):
    """Timestamps of slurmctld log lines matching an extended regex.

    The log is only cleared once per module, and a mid-module
    start_slurmctld(clean=True) resets the job id counter, so a later test can
    match an earlier test's lines. Pass `since` to ignore anything older.
    """
    logfile = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    output = atf.run_command_output(
        f"grep -E -- '{pattern}' {logfile} || true",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    lines = output.splitlines()
    stamps = []
    for line in lines:
        match = re.match(r"\[([\d\-T:.]+)\]", line)
        if match:
            stamps.append(datetime.fromisoformat(match.group(1)))
    # LogTimeFormat is pinned in the fixture, so lines that match the pattern
    # must also parse. Failing loudly beats returning an empty list, which
    # would surface as "slurmctld never logged ..." on a config mismatch.
    if lines and not stamps:
        pytest.fail(f"Could not parse a timestamp from: {lines[0]!r}")
    if since:
        stamps = [stamp for stamp in stamps if stamp >= since]
    return stamps


def verify_launched_by_poll(job_id, nodes, since=None):
    """Verify job_time_limit() drove the launch, not the node registration."""
    registered = logged_times("Node (%s) now responding" % "|".join(nodes), since)
    completed = logged_times(f"Configuration for JobId={job_id} complete", since)
    assert registered, "slurmctld never logged a node registration"
    assert completed, f"slurmctld never completed configuration for JobId={job_id}"
    polled = [
        stamp
        for stamp in logged_times("Testing job time limits", since)
        if registered[-1] < stamp <= completed[-1]
    ]
    assert polled, (
        f"JobId={job_id} was launched without job_time_limit() running since "
        "its last node registered, so the prompt launch happened even though "
        "fast_power_up_launch is not configured"
    )


def test_job_launches_on_the_periodic_poll():
    """A job whose node powers up is launched by the next job_time_limit()."""

    submitted = datetime.now()
    job_id = atf.submit_job_sbatch(
        f"-p cloud -w {node1} --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_node_state(
        node1, "POWERING_UP", timeout=resume_timeout
    ), f"{node1} should be POWERING_UP for the job"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # ResumeProgram is /bin/true, so the node only finishes powering up once a
    # slurmd is registered for it.
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node1}",
        user="root",
        fatal=True,
    )
    assert atf.wait_for_node_state(
        node1, "POWERING_UP", reverse=True, timeout=resume_timeout
    ), f"{node1} should leave POWERING_UP once its slurmd registers"

    # The launch is the next poll's job, so allow a full PERIODIC_TIMEOUT.
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=atf.PERIODIC_TIMEOUT + 15, poll_interval=0.5
    ), "Job should still launch on the periodic poll without the option"

    verify_launched_by_poll(job_id, [node1], submitted)
