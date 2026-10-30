############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test switch/lua handling of in-flight hooks across a slurmctld restart."""

import os
import pathlib
import time

import pytest

import atf

NODE_COUNT = 2

# Short, so a stuck teardown does not starve later tests of nodes.
SWITCH_TIMEOUT = 10

# Switch deadlines and retries are acted on every PERIODIC_TIMEOUT.
SWITCH_TICK = 30

MIN_JOB_AGE = 10

# A retrying teardown must still be retrying when slurmctld is killed.
SWITCH_RETRIES = 5

SHARED_DIR = None
START_GATE_FILE = None
COMPLETE_GATE_FILE = None
MARKER_DIR = None

DIRECTIVE_PREFIX = "switchlua:"

# A --network value that has the job set up on the default path.
MANAGED = "managed"

switch_lua = f"""
local START_GATE = "@START_GATE_FILE@"
local COMPLETE_GATE = "@COMPLETE_GATE_FILE@"
local MARKER_DIR = "@MARKER_DIR@"

local function exists(path)
    local f = io.open(path, "r")

    if f == nil then
        return false
    end

    f:close()

    return true
end

-- Block while the gate file exists, capped in case a test fails.
local function hold(path)
    local deadline = os.time() + 120

    while os.time() < deadline and exists(path) do
        for _ = 1, 200000 do end
    end
end

local function note(name, line)
    local f = io.open(MARKER_DIR .. "/" .. name, "a")

    if f ~= nil then
        f:write(line .. "\\n")
        f:close()
    end
end

local function directive(network)
    if network == nil then
        return ""
    end

    return string.match(network, "{DIRECTIVE_PREFIX}([%w_]+)") or ""
end

-- Hold the nodes until the teardown reports (not the default).
function slurm_switch_block_job_complete()
    return true
end

function slurm_switch_job_start(job)
    local mode = directive(job.network)

    note(string.format("start.%d", job.job_id), tostring(job.sluid))

    if mode == "start_hold" then
        hold(START_GATE)
    end

    if mode == "decline" then
        return slurm.SUCCESS, nil
    end

    return slurm.SUCCESS, {{ mode = mode }}
end

function slurm_switch_job_complete(job)
    local mode = directive(job.network)

    note(string.format("complete.%d", job.job_id), tostring(job.sluid))

    if mode == "complete_hold" then
        hold(COMPLETE_GATE)
    end

    -- Ask to be retried while the gate file exists.
    if mode == "complete_retry" and exists(COMPLETE_GATE) then
        return slurm.EAGAIN
    end

    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, START_GATE_FILE, COMPLETE_GATE_FILE, MARKER_DIR

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    START_GATE_FILE = f"{SHARED_DIR}/start_gate"
    COMPLETE_GATE_FILE = f"{SHARED_DIR}/complete_gate"
    MARKER_DIR = f"{SHARED_DIR}/markers"

    atf.require_version(
        (26, 11), "sbin/slurmd", reason="Issue 51019: switch/lua was added in 26.11"
    )
    atf.require_build_config(
        "HAVE_LUA",
        reason="Issue 51019: Slurm was built without lua, so switch/lua is absent",
    )
    atf.require_nodes(NODE_COUNT)
    atf.require_config_parameter("SwitchType", "switch/lua")
    atf.require_config_parameter("MinJobAge", MIN_JOB_AGE)
    atf.require_config_parameter_includes("DebugFlags", "Switch")
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("switch_timeout", SWITCH_TIMEOUT)
    )
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("switch_retries", SWITCH_RETRIES)
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file(
        "switch.lua",
        switch_lua.replace("@START_GATE_FILE@", START_GATE_FILE)
        .replace("@COMPLETE_GATE_FILE@", COMPLETE_GATE_FILE)
        .replace("@MARKER_DIR@", MARKER_DIR),
    )

    os.makedirs(MARKER_DIR)
    os.chmod(SHARED_DIR, 0o777)
    os.chmod(MARKER_DIR, 0o777)

    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def clear_jobs():
    """Cancel all jobs and wait for the nodes to be idle again."""

    yield

    atf.cancel_all_jobs(quiet=True)

    assert atf.wait_for_node_state(
        sorted(atf.nodes), "IDLE", operator=atf.SetMatch.EQUAL
    ), f"Nodes did not come back after this test: {atf.get_nodes(quiet=True)}"


@pytest.fixture(scope="function")
def resume_drained_nodes():
    """Resume any node drained by a test."""

    yield

    for node, node_dict in atf.get_nodes(quiet=True).items():
        if {"DOWN", "DRAIN", "FAIL"} & set(node_dict["state"]):
            atf.run_command(
                f"scontrol update nodename={node} state=resume",
                user="root",
                quiet=True,
            )


@pytest.fixture(scope="function")
def start_gate():
    """Hold slurm_switch_job_start() for start_hold jobs until released."""

    path = pathlib.Path(START_GATE_FILE)
    path.touch()
    os.chmod(START_GATE_FILE, 0o666)

    yield lambda: path.unlink(missing_ok=True)

    path.unlink(missing_ok=True)


@pytest.fixture(scope="function")
def complete_gate():
    """Hold complete_hold and retry complete_retry teardowns until released."""

    path = pathlib.Path(COMPLETE_GATE_FILE)
    path.touch()
    os.chmod(COMPLETE_GATE_FILE, 0o666)

    yield lambda: path.unlink(missing_ok=True)

    path.unlink(missing_ok=True)


@pytest.fixture(scope="function")
def slow_prolog_slurmctld(tmp_path):
    """Hold new jobs in CONFIGURING with a slow PrologSlurmctld."""

    prolog = str(tmp_path / "prolog_slurmctld.sh")
    atf.make_bash_script(prolog, "sleep 120")

    atf.set_config_parameter("PrologSlurmctld", prolog, restart=True)

    yield

    atf.set_config_parameter("PrologSlurmctld", None, restart=True)


def crash_slurmctld():
    """Kill slurmctld (and slurmscriptd) without a graceful shutdown.

    A shutdown signals PrologSlurmctld, which is treated as success, so the
    job would launch before state is saved.
    """

    assert not atf.stop_slurmctld(signal="KILL"), "slurmctld survived being killed"


def save_job_state(job_id):
    """Force a job state save and wait for it to complete.

    The job must still be updatable. mtime is compared at sub-second
    precision, as the save can land in the same second as the update.
    """

    state_file = f"{atf.get_config_parameter('StateSaveLocation')}/job_state"
    since = time.time()

    atf.run_command(
        f"scontrol update jobid={job_id} comment=save_{int(since)}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    for t in atf.timer():
        mtime = atf.run_command_output(
            f"stat -c %.9Y {state_file}", user="root", quiet=True
        ).strip()
        if float(mtime or 0) >= since:
            return True

    return False


def marker_lines(path):
    """Return the lines a hook recorded, or an empty list."""

    return atf.run_command_output(f"cat {path}", quiet=True).split()


def test_job_start_is_rerun_for_an_unfinished_setup(start_gate, resume_drained_nodes):
    """Test that a setup outstanding at a crash is run again.

    slurmctld is killed, so it restarts from the state saved during the setup.
    """

    job_id = atf.submit_job_sbatch(
        f'-N1 --network={DIRECTIVE_PREFIX}start_hold --wrap "true"', fatal=True
    )

    assert atf.wait_for_job_state(
        job_id, "CONFIGURING", fatal=True
    ), f"Job {job_id} never reached CONFIGURING"

    marker = f"{MARKER_DIR}/start.{job_id}"
    for t in atf.timer():
        if len(marker_lines(marker)) >= 1:
            break
    else:
        pytest.fail(f"slurm_switch_job_start() did not run for job {job_id}")

    assert save_job_state(job_id), (
        "Job state was never written out while the setup was outstanding, so"
        " the crash would not capture it"
    )

    crash_slurmctld()
    atf.start_slurmctld()

    assert atf.wait_for_job_state(job_id, "CONFIGURING"), (
        f"Job {job_id} did not come back configuring, so the state the crash"
        " captured was not the state the test meant to create"
    )

    for t in atf.timer():
        if len(marker_lines(marker)) >= 2:
            break
    else:
        pytest.fail(
            f"slurm_switch_job_start() was not run again for job {job_id}, whose"
            " setup was outstanding when slurmctld went down"
        )

    # The rerun has a fresh switch_timeout deadline.
    start_gate()

    assert atf.wait_for_job_state(job_id, "COMPLETED"), (
        f"Job {job_id} did not run once the re-run slurm_switch_job_start()"
        " had answered"
    )


@pytest.mark.parametrize(
    "network",
    [
        pytest.param(f"{DIRECTIVE_PREFIX}decline", id="declined"),
        pytest.param(MANAGED, id="with_jobinfo"),
    ],
)
def test_job_start_is_not_rerun_for_a_finished_setup(slow_prolog_slurmctld, network):
    """Test that a finished setup is not rerun for a job still configuring."""

    job_id = atf.submit_job_sbatch(f'-N1 --network={network} --wrap "true"', fatal=True)

    assert atf.wait_for_job_state(
        job_id, "CONFIGURING", fatal=True
    ), f"Job {job_id} never reached CONFIGURING"

    marker = f"{MARKER_DIR}/start.{job_id}"
    for t in atf.timer():
        if len(marker_lines(marker)) >= 1:
            break
    else:
        pytest.fail(f"slurm_switch_job_start() did not run for job {job_id}")

    # slurmctld records the answer after the marker is written.
    for t in atf.timer():
        if f"switch setup done for JobId={job_id}\n" in atf.get_log("slurmctld"):
            break
    else:
        pytest.fail(f"slurmctld did not record the setup of job {job_id}")

    assert save_job_state(job_id), "Job state was never written out"

    crash_slurmctld()
    atf.start_slurmctld()

    assert atf.wait_for_job_state(job_id, "CONFIGURING"), (
        f"Job {job_id} did not come back configuring, so the state the crash"
        " captured was not the state the test meant to create"
    )

    # A rerun would start while state is loaded, so a short wait suffices.
    for t in atf.timer(timeout=3, xfail=True):
        if len(marker_lines(marker)) >= 2:
            pytest.fail(
                f"slurm_switch_job_start() was run again for job {job_id}, whose setup"
                " had already finished"
            )


def test_running_job_is_torn_down_once_after_a_restart(resume_drained_nodes):
    """Test that a job running across a restart is torn down once."""

    job_id = atf.submit_job_sbatch(
        f'-N1 --network={MANAGED} --wrap "sleep infinity"', fatal=True
    )

    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), f"Job {job_id} did not start"

    start_marker = f"{MARKER_DIR}/start.{job_id}"
    complete_marker = f"{MARKER_DIR}/complete.{job_id}"
    for t in atf.timer():
        if len(marker_lines(start_marker)) >= 1:
            break
    else:
        pytest.fail(f"slurm_switch_job_start() did not run for job {job_id}")

    atf.restart_slurmctld()

    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), f"Job {job_id} did not survive the restart"

    assert not marker_lines(
        complete_marker
    ), f"slurm_switch_job_complete() ran for job {job_id} while it was running"

    atf.run_command(f"scancel {job_id}", fatal=True)

    for t in atf.timer():
        if len(marker_lines(complete_marker)) >= 1:
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() never ran for job {job_id}, so the"
            " jobinfo that marks it as managed did not survive the restart"
        )

    # Exact, as a held node also reports IDLE.
    assert atf.wait_for_node_state(
        sorted(atf.nodes), "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"Nodes did not come back after job {job_id} was torn down"

    assert (
        len(marker_lines(complete_marker)) == 1
    ), f"slurm_switch_job_complete() was run more than once for job {job_id}"

    assert (
        len(marker_lines(start_marker)) == 1
    ), f"slurm_switch_job_start() was run again for job {job_id}"

    sluid = marker_lines(start_marker)[0]
    assert sluid != "nil", f"slurm_switch_job_start() got no job.sluid for job {job_id}"
    assert marker_lines(complete_marker) == [sluid], (
        f"slurm_switch_job_complete() got job.sluid"
        f" {marker_lines(complete_marker)} for job {job_id}, whose setup got"
        f" {sluid}"
    )


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (25, 11),
    reason="Issue 51019: scontrol shows the SLUID of a job from 25.11",
)
@pytest.mark.parametrize(
    "network",
    [
        pytest.param(f"{DIRECTIVE_PREFIX}complete_hold", id="running"),
        pytest.param(f"{DIRECTIVE_PREFIX}complete_retry", id="waiting_to_retry"),
    ],
)
def test_outstanding_teardown_is_rerun_after_a_restart(
    network, complete_gate, resume_drained_nodes
):
    """Test that a teardown outstanding at a crash is run again, nodes held.

    A requeue held in COMPLETING by it must finish once the rerun succeeds,
    which is detected by the SLUID changing.
    """

    retry = network.endswith("complete_retry")

    job_id = atf.submit_job_sbatch(
        f'-N1 --network={network} --wrap "sleep infinity"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "RUNNING", fatal=True
    ), f"Job {job_id} did not start"

    # A completing job lists only nodes still running their epilog.
    node = atf.get_job_parameter(job_id, "NodeList")
    sluid_before = atf.get_job_parameter(job_id, "SLUID")
    assert sluid_before, f"Job {job_id} has no SLUID"

    # Hold so the job is not scheduled again.
    atf.run_command(
        f"scontrol requeuehold {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    marker = f"{MARKER_DIR}/complete.{job_id}"
    for t in atf.timer():
        if len(marker_lines(marker)) >= 1:
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() did not run for requeued job {job_id},"
            " so there is no outstanding teardown to rerun"
        )

    # Wait for every epilog, so only the teardown holds the job at the crash.
    for t in atf.timer():
        if not atf.get_job_parameter(job_id, "NodeList", quiet=True):
            break
    else:
        pytest.fail(
            f"The epilog of job {job_id} did not finish while its teardown was held"
        )

    assert save_job_state(job_id), (
        "Job state was never written out after the teardown began, so the"
        " crash would not capture it"
    )

    assert (
        atf.get_job_parameter(job_id, "JobState") == "COMPLETING"
    ), f"Job {job_id} was no longer completing when the crash was due"

    crash_slurmctld()

    runs = len(marker_lines(marker))

    atf.start_slurmctld()

    # The gate still holds or retries the rerun.
    for t in atf.timer():
        if len(marker_lines(marker)) > runs:
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() was not run again for job {job_id},"
            " whose teardown was holding its node when slurmctld went down"
        )

    states = atf.get_nodes(quiet=True)[node]["state"]
    assert "COMPLETING" in states, (
        f"{node} was not held while the rerun teardown of job {job_id} was"
        f" outstanding: {states}"
    )

    # The rerun has a fresh switch_timeout deadline.
    complete_gate()

    # Exact, as a held or drained node also reports IDLE. A retry comes at the
    # next periodic check.
    assert atf.wait_for_node_state(
        node,
        "IDLE",
        operator=atf.SetMatch.EQUAL,
        timeout=SWITCH_TICK + 15,
        fatal=True,
    ), f"{node} did not come back once the rerun teardown of job {job_id} succeeded"

    for _ in atf.timer():
        sluid_after = atf.get_job_parameter(job_id, "SLUID", quiet=True)
        if sluid_after != sluid_before:
            break
    else:
        pytest.fail(
            f"Job {job_id} still has SLUID {sluid_before} after the rerun of"
            " its teardown succeeded, so its requeue was never finished"
        )

    assert (
        atf.get_job_parameter(job_id, "JobState") == "PENDING"
    ), f"Job {job_id} was not left pending by a requeue hold"

    # The rerun, and for a retrying teardown the retry that succeeded.
    reruns = len(marker_lines(marker)) - runs
    assert reruns == (2 if retry else 1), (
        f"slurm_switch_job_complete() was run {reruns} times for job {job_id}"
        " after the restart"
    )


def test_job_ended_during_setup_is_torn_down_after_a_restart(
    start_gate, resume_drained_nodes
):
    """Test that a job ended during an outstanding setup is torn down after a crash.

    The setup never reported, so it is torn down as an abandoned setup. The
    ended job cannot be updated to force a save, so a held job is.
    """

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}start_hold --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "CONFIGURING", fatal=True
    ), f"Job {job_id} never reached CONFIGURING"

    start_marker = f"{MARKER_DIR}/start.{job_id}"
    complete_marker = f"{MARKER_DIR}/complete.{job_id}"
    for t in atf.timer():
        if len(marker_lines(start_marker)) >= 1:
            break
    else:
        pytest.fail(f"slurm_switch_job_start() did not run for job {job_id}")

    atf.run_command(f"scancel {job_id}", fatal=True)

    other_id = atf.submit_job_sbatch('-N1 -H --wrap "true"', fatal=True)

    assert save_job_state(other_id), (
        "Job state was never written out after the job ended, so the crash"
        " would not capture it"
    )

    assert not marker_lines(complete_marker), (
        f"slurm_switch_job_complete() ran for job {job_id} before the crash,"
        " so its setup was no longer outstanding"
    )

    crash_slurmctld()
    atf.start_slurmctld()

    for t in atf.timer():
        if len(marker_lines(complete_marker)) >= 1:
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() was not run for job {job_id}, whose"
            " setup was outstanding when slurmctld went down"
        )

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"{node} did not come back once job {job_id} was torn down"

    assert (
        len(marker_lines(complete_marker)) == 1
    ), f"slurm_switch_job_complete() was run more than once for job {job_id}"

    assert (
        len(marker_lines(start_marker)) == 1
    ), f"slurm_switch_job_start() was run again for job {job_id}, which had ended"

    assert marker_lines(complete_marker) == marker_lines(start_marker), (
        f"slurm_switch_job_complete() got job.sluid"
        f" {marker_lines(complete_marker)} for job {job_id}, whose setup got"
        f" {marker_lines(start_marker)}"
    )
