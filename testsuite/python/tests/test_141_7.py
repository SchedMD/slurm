############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Job launches promptly once its powering-up nodes register.

A job that has to power up nodes waits for the next periodic poll of
job_time_limit() in the slurmctld background thread (every PERIODIC_TIMEOUT
seconds) before launching, even after all of its nodes have finished booting.
With SlurmctldParameters=fast_power_up_launch, validate_node_specs() launches a
waiting job as soon as the last of its nodes registers.

That launch happens in the same validate_node_specs() call that clears
POWERING_UP, under one job/node write lock, so with no Prolog configured (as
here) the job is already RUNNING by the earliest moment a client can observe
the node out of POWERING_UP. Asserting that is much tighter than a wall-clock
threshold, which cannot work at all here: a poll-only launch lands anywhere in
[0, PERIODIC_TIMEOUT) after registration and would slip under any threshold
loose enough not to be flaky.

Sampling those two states separately would leave a race: a poll-only launch
landing between the node read and the job read looks identical to a prompt one.
So the launch is confirmed from the controller's own log instead, by checking
that no job_time_limit() poll ran between the last node registering and the
job's configuration completing. Both timestamps come from one clock, and the
poll is either present in that interval or it is not, so the check does not
depend on when the test happens to look.
"""

import re
import time
from datetime import datetime

import pytest

import atf

node1 = "node1"
node2 = "node2"
resume_timeout = 60
suspend_time = 60

# Window for the negative check that a job does NOT launch while one of its
# nodes is still powering up. The launch path is synchronous with registration,
# so an erroneous launch would happen almost immediately and a short window
# catches it.
not_launched_window = 5


# Maximum gap, in the controller's own log, between the last node registering
# and the job's configuration completing. The gate runs inside the same
# validate_node_specs() call that logs the node as responding, so the two land
# together; a poll-driven completion cannot be nearer than the next
# PERIODIC_TIMEOUT boundary.
launch_on_registration_window = 1


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Runs slurmd on same machine as slurmctld")
    # The prompt-launch-on-registration behavior lives in slurmctld; older
    # controllers only launch on the periodic job_time_limit() poll.
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 51050: slurmctld launches a job from node registration "
        "only as of 26.11",
    )
    # Prompt launch is opt-in; without this the job only starts on the next
    # periodic job_time_limit() poll.
    atf.require_config_parameter_includes("SlurmctldParameters", "fast_power_up_launch")
    # The poll announces itself at debug2; the list form means an environment
    # already more verbose than that is not downgraded.
    atf.require_config_parameter(
        "SlurmctldDebug", ["debug2", "debug3", "debug4", "debug5"]
    )
    # logged_times() parses the bracketed ISO timestamp.
    atf.require_config_parameter("LogTimeFormat", "iso8601_ms")
    # A PrologSlurmctld still running when the last node registers defers the
    # launch to prolog_running_decr(), which logs a different message than the
    # one verify_launched_on_registration() matches.
    atf.require_config_parameter("PrologSlurmctld", None)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)

    # Two static cloud nodes, which come up POWERED_DOWN.
    # Distinct ports so both slurmds can run on the same host at once.
    atf.require_config_parameter(
        "NodeName",
        {
            node1: {"State": "CLOUD", "Port": 6821},
            node2: {"State": "CLOUD", "Port": 6822},
        },
    )
    atf.require_config_parameter(
        "PartitionName", {"cloud": {"Nodes": f"{node1},{node2}"}}
    )

    # Tests start slurmds manually, so don't wait for the cloud nodes to come up.
    atf.start_slurmctld(clean=True)

    yield

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


def verify_launched_on_registration(job_id, nodes, since=None):
    """Verify the registration drove the launch, not the periodic poll.

    job_time_limit() and launch_ready_jobs_on_node() both reach
    _test_job_config_complete(), so its log message alone does not say which
    one fired -- but the timing does. Comparing two controller-side timestamps
    also avoids the race in sampling node state and job state separately, where
    a poll landing between the two reads would look like a prompt launch.
    """
    registered = logged_times("Node (%s) now responding" % "|".join(nodes), since)
    completed = logged_times(f"Configuration for JobId={job_id} complete", since)
    assert registered, "slurmctld never logged a node registration"
    assert completed, (
        f"JobId={job_id} was still configuring once its last node had "
        "registered, so the registration did not complete its configuration"
    )
    last_registration, launched = registered[-1], completed[-1]
    polled = [
        stamp
        for stamp in logged_times("Testing job time limits", since)
        if last_registration < stamp <= launched
    ]
    assert not polled, (
        f"job_time_limit() ran between the last registration and the launch of "
        f"JobId={job_id}, so the periodic poll cannot be ruled out as the "
        "cause; this run proves nothing either way"
    )
    delay = (launched - last_registration).total_seconds()
    assert delay >= 0, (
        f"JobId={job_id} finished configuring before its last node registered, "
        "so it did not wait for all of its nodes"
    )
    assert delay < launch_on_registration_window, (
        f"JobId={job_id} was launched {delay:.1f}s after its last node "
        f"registered, so the registration did not trigger it "
        f"(PERIODIC_TIMEOUT={atf.PERIODIC_TIMEOUT}s)"
    )


@pytest.fixture(autouse=True)
def reset_state():
    """Reset the controller and cloud node state between tests."""
    yield
    # conftest only cancels jobs when it started Slurm itself (it keys off
    # properties["slurm-started"], which require_slurm_running() sets and
    # start_slurmctld() does not), so cancel them here. Otherwise the running
    # job's slurmstepd is orphaned by the shutdown below and outlives the test.
    atf.cancel_all_jobs(fatal=True)

    # Cleanly stop the manually-started slurmds along with the controller, then
    # start fresh so the cloud nodes come back POWERED_DOWN for the next test.
    # stop_slurmctld() polls for exit and captures cores on a hang.
    atf.stop_slurmctld(also_slurmds=True)
    atf.start_slurmctld(clean=True)


def register_slurmd(node_name):
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_name}",
        user="root",
        fatal=True,
    )


def register_and_verify_launch(job_id, node_name, since=None):
    """Register a powering-up node and verify the job launched along with it.

    slurmctld clears POWERING_UP and launches the job in the same
    validate_node_specs() call while holding the job and node write locks, so
    the job is already RUNNING by the time the node is observable as powered up.
    A poll-only launch would instead leave the job CONFIGURING for up to
    PERIODIC_TIMEOUT after the node came up.
    """
    register_slurmd(node_name)
    # timeout covers ResumeTimeout, since the node is still powering up here.
    # poll_interval is tightened so little time passes between the node leaving
    # POWERING_UP and the job state read below: a periodic job_time_limit()
    # launch landing inside that gap would mask the regression this detects.
    assert atf.wait_for_node_state(
        node_name,
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout,
        poll_interval=0.5,
    ), f"{node_name} should leave POWERING_UP once its slurmd registers"
    verify_launched_on_registration(job_id, [node_name], since)
    assert (
        atf.get_job_parameter(job_id, "JobState") == "RUNNING"
    ), "Job should already be RUNNING once its last node has powered up"


def test_job_launches_promptly_on_power_up():
    """A single-node job launches as soon as its node powers up."""

    # Submitting the job triggers the power up of its node.
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

    # ResumeProgram is /bin/true, so the node only finishes powering up once we
    # register a slurmd for it. The job must launch with that registration,
    # rather than waiting for the next periodic job_time_limit() poll.
    register_and_verify_launch(job_id, node1, submitted)


def test_job_launches_on_last_node_power_up():
    """A multi-node job launches only once its last node powers up, and does so
    promptly when that final node registers."""

    # --wait-all-nodes=0 asks for the sbatch node-zero start, which is exactly
    # what a job waiting on node power up must ignore: reboot_job_nodes()
    # overwrites the submitted value with wait_all_nodes = 1. Passing 1 instead
    # would satisfy the assertions below by itself, leaving the forcing
    # untested.
    submitted = datetime.now()
    job_id = atf.submit_job_sbatch(
        f"-p cloud -N2 -w {node1},{node2} --wait-all-nodes=0 --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_node_state(
        [node1, node2], "POWERING_UP", timeout=resume_timeout
    ), "Both nodes should be POWERING_UP for the job"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Registering only the first node must NOT launch the job: the second node
    # is still powering up.
    register_slurmd(node1)
    # slurmd -b forks and returns before it has registered, so wait for node1 to
    # actually come up. Without this, the checks below can be satisfied while no
    # node has registered at all, which is a weaker property than the one being
    # tested and one the feature could not violate.
    assert atf.wait_for_node_state(
        node1, "POWERING_UP", reverse=True, timeout=resume_timeout
    ), f"{node1} should leave POWERING_UP once its slurmd registers"
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=not_launched_window, xfail=True
    ), "Job should not launch until all of its nodes have powered up"
    # wait_for_job_state() also returns False for a job that ended, so assert
    # the job is actually still waiting rather than dead.
    assert (
        atf.get_job_parameter(job_id, "JobState") == "CONFIGURING"
    ), "Job should still be CONFIGURING while its second node is powering up"

    # Registering the last node should launch the job with that registration.
    register_and_verify_launch(job_id, node2, submitted)
