############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A job that reboots its nodes for a feature change launches promptly.

SlurmctldParameters=fast_power_up_launch is documented to cover a reboot
requested by a node feature change, not only sbatch --reboot and resumes from
power save. reboot_job_nodes() builds its boot list from
node_features_reboot() in that case and marks those nodes
NODE_STATE_POWERING_UP, which is the flag validate_node_specs() gates the
prompt launch on, so all three triggers share one code path.

Rather than timing the launch -- a poll-only launch lands anywhere in
[0, PERIODIC_TIMEOUT) and would slip under any threshold loose enough not to
be flaky -- this asserts a state invariant: the job is already RUNNING by the
time the last of its nodes has left POWERING_UP. The node state and the job
state would be a race if sampled separately, so the launch is confirmed from
the controller's own log: no job_time_limit() poll may run between the last
node registering and the job's configuration completing.
"""

import re
from datetime import datetime
from pathlib import Path

import pytest

import atf

resume_timeout = 120

# Seconds each node waits before its slurmd comes back. The gap between them is
# the window in which the job must stay CONFIGURING, having only some of its
# nodes.
early_boot = 5
late_boot = 20

# How long to watch for a launch that must not happen, while the last node is
# still rebooting.
not_launched_window = 5
slurmd_bin = atf.properties["slurm-sbin-dir"] + "/slurmd"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Runs slurmd on same machine as slurmctld")
    # The prompt-launch-on-registration behavior lives in slurmctld; older
    # controllers only launch on the periodic job_time_limit() poll. The gate
    # names slurmd rather than slurmctld because this module also restarts each
    # node's slurmd through a RebootProgram, which a node left on an older
    # slurmd by a partial upgrade does not survive. slurmd is never newer than
    # slurmctld, so requiring it also requires the controller.
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="Issue 51050: slurmctld launches a job from node registration "
        "only as of 26.11, and the reboot helper needs a matching slurmd",
    )
    # Prompt launch is opt-in; without this the job only starts on the next
    # periodic job_time_limit() poll.
    atf.require_config_parameter_includes("SlurmctldParameters", "fast_power_up_launch")
    atf.require_config_parameter_includes(
        "NodeFeaturesPlugins", "node_features/helpers"
    )
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
    atf.require_nodes(2)
    # Rebooted nodes must come back into service on their own, and must be
    # given long enough to do so; the late node sleeps late_boot seconds.
    atf.require_config_parameter("ReturnToService", 2)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    helper = helper_script(
        f"{atf.module_tmp_path}/helper.sh", f"{atf.module_tmp_path}/helper.vars"
    )
    atf.require_config_parameter(
        "Feature",
        {"f1": {"Helper": helper}, "f2": {"Helper": helper}},
        source="helpers",
    )
    atf.require_config_parameter_includes("DebugFlags", "POWER")
    # SlurmdPidFile carries a %n placeholder; the reboot script runs on the
    # node being rebooted, so substitute the name slurmd exports there.
    pidfile = atf.get_config_parameter("SlurmdPidFile", live=False)
    assert pidfile, "SlurmdPidFile must be configured to stop slurmd by pidfile"
    atf.require_config_parameter(
        "RebootProgram",
        reboot_script(
            f"{atf.module_tmp_path}/reboot.sh",
            pidfile.replace("%n", "$SLURM_NODE_NAME"),
        ),
    )
    atf.require_slurm_running()


def helper_script(path, var_file):
    """Report the node's active feature, or set it when given an argument."""
    atf.make_bash_script(
        path,
        f"""
touch {var_file}
if [ -n "$1" ]; then
    echo $1 > {var_file}
fi
cat {var_file}""",
    )
    # Boot into f1 so that requesting f2 forces a reboot.
    atf.run_command(f"echo f1 > {var_file}", fatal=True, quiet=True)
    atf.run_command(f"chmod 0666 {var_file}", user="root", fatal=True, quiet=True)
    return path


def reboot_script(path, pidfile):
    """Fake a reboot: stop this node's slurmd, pause, then start it again.

    The pause is read from a per-node file so a test can stagger the nodes and
    watch the job while only some of them have come back.
    """
    atf.make_bash_script(
        path,
        f"""
exec &> >(tee -a {Path(path).parent}/{Path(path).stem}.log)
set -x
delay=$(cat {Path(path).parent}/delay.$SLURM_NODE_NAME 2>/dev/null || echo {early_boot})
sudo pkill -F {pidfile}
sleep $delay
sudo {slurmd_bin} -N $SLURM_NODE_NAME -b
""",
    )
    return path


def stagger_boots(nodes):
    """Make the last node take much longer to come back than the first."""
    for node, delay in zip(nodes, (early_boot, late_boot)):
        atf.run_command(
            f"echo {delay} > {atf.module_tmp_path}/delay.{node}", fatal=True, quiet=True
        )
        atf.run_command(
            f"chmod 0644 {atf.module_tmp_path}/delay.{node}",
            user="root",
            fatal=True,
            quiet=True,
        )


# Maximum gap, in the controller's own log, between the last node registering
# and the job's configuration completing. The gate runs inside the same
# validate_node_specs() call that logs the node as responding, so the two land
# together; a poll-driven completion cannot be nearer than the next
# PERIODIC_TIMEOUT boundary.
launch_on_registration_window = 1


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


def test_job_launches_when_last_feature_rebooted_node_registers():
    """A feature-change job is RUNNING by the time its last node registers."""

    nodes = list(atf.get_nodes(quiet=True).keys())[:2]
    for node in nodes:
        atf.wait_for_node_state(node, "IDLE", fatal=True)

    stagger_boots(nodes)

    # The nodes are active in f1, so requesting f2 makes slurmctld reboot them
    # through node_features_reboot() rather than through power save.
    submitted = datetime.now()
    job_id = atf.submit_job_sbatch(
        f"-w {atf.node_list_to_range(nodes)} -N{len(nodes)} -C f2"
        " --wrap 'sleep infinity'",
        fatal=True,
    )

    for node in nodes:
        assert atf.wait_for_node_state(
            node, "POWERING_UP", timeout=60
        ), f"{node} should be POWERING_UP while it reboots for the feature change"

    # The first node comes back well before the last one, so the job must sit
    # there with only part of its allocation registered.
    assert atf.wait_for_node_state(
        nodes[0], "POWERING_UP", reverse=True, timeout=resume_timeout
    ), f"{nodes[0]} should leave POWERING_UP once its slurmd registers"
    assert not atf.wait_for_job_state(
        job_id, "RUNNING", timeout=not_launched_window, xfail=True
    ), "Job should not launch until all of its nodes have rebooted"
    # wait_for_job_state() also returns False for a job that ended, so assert
    # the job is actually still waiting rather than dead.
    assert (
        atf.get_job_parameter(job_id, "JobState") == "CONFIGURING"
    ), "Job should still be CONFIGURING while its last node is rebooting"

    # The last registration is what finishes the job's configuration, so the
    # job must already be RUNNING once no node is left in POWERING_UP.
    assert atf.wait_for_node_state(
        nodes[-1],
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout,
        poll_interval=0.5,
    ), f"{nodes[-1]} should leave POWERING_UP once its slurmd registers"

    verify_launched_on_registration(job_id, nodes, submitted)
    assert (
        atf.get_job_parameter(job_id, "JobState") == "RUNNING"
    ), "Job should already be RUNNING once its last node has rebooted"
