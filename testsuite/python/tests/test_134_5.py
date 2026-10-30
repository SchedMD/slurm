############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test switch/lua when slurm_switch_block_job_complete() returns false."""

import os
import pathlib

import pytest

import atf

NODE_COUNT = 2

# Switch deadlines and retries are acted on every PERIODIC_TIMEOUT.
SWITCH_TICK = 30

SWITCH_TIMEOUT = 10

# Must exceed SWITCH_TIMEOUT, or the job record can be purged before the
# timeout drains the nodes.
MIN_JOB_AGE = 20

# How long a node must stay released; must fit well inside SWITCH_TIMEOUT.
RELEASE_PROOF = 2

# How long a node must stay held; must fit well inside SWITCH_TIMEOUT.
HOLD_PROOF = 2

# How long slurmctld may take to act on a report; short enough that the
# switch_timeout deadline cannot be what ends the wait.
REPORT_WAIT = 3

SHARED_DIR = None
GATE_FILE = None
ATTEMPTS = None

DRAIN_REASON = "Switch job complete failed"

DIRECTIVE_PREFIX = "switchlua:"

switch_lua = f"""
local GATE = "@GATE_FILE@"
local ATTEMPTS = "@ATTEMPTS@"

-- Block while the gate file exists, capped in case a test fails.
local function hold()
    local deadline = os.time() + 120

    while os.time() < deadline do
        local gate = io.open(GATE, "r")

        if gate == nil then
            return
        end

        gate:close()

        for _ = 1, 200000 do end
    end
end

-- Record a job_complete call and return the number of calls so far.
local function attempt(job_id)
    local path = ATTEMPTS .. "." .. job_id
    local f = io.open(path, "a")

    if f ~= nil then
        f:write("x\\n")
        f:close()
    end

    local calls = 0

    f = io.open(path, "r")

    if f ~= nil then
        for _ in f:lines() do
            calls = calls + 1
        end
        f:close()
    end

    return calls
end

local function directive(network)
    if network == nil then
        return ""
    end

    return string.match(network, "{DIRECTIVE_PREFIX}([%w_]+)") or ""
end

function slurm_switch_block_job_complete()
    return false
end

function slurm_switch_job_start(job)
    local mode = directive(job.network)

    if mode == "start_hold_retry" then
        hold()
    end

    return slurm.SUCCESS, {{ mode = mode }}
end

function slurm_switch_job_complete(job)
    local mode = directive(job.network)

    if mode == "hold" or mode == "timeout" then
        hold()
    end

    if mode == "fail" then
        return -1
    end

    if mode == "eagain" then
        return slurm.EAGAIN
    end

    -- Ask to be retried once.
    if mode == "start_hold_retry" and attempt(job.job_id) == 1 then
        return slurm.EAGAIN
    end

    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, GATE_FILE, ATTEMPTS

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    GATE_FILE = f"{SHARED_DIR}/gate"
    ATTEMPTS = f"{SHARED_DIR}/attempts"

    atf.require_version(
        (26, 11), "sbin/slurmd", reason="Issue 51019: switch/lua was added in 26.11"
    )
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 51019: switch/lua was added in 26.11"
    )
    atf.require_build_config(
        "HAVE_LUA",
        reason="Issue 51019: Slurm was built without lua, so switch/lua is absent",
    )
    atf.require_nodes(NODE_COUNT)
    atf.require_config_parameter("SwitchType", "switch/lua")
    atf.require_config_parameter("MinJobAge", MIN_JOB_AGE)
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("switch_timeout", SWITCH_TIMEOUT)
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file(
        "switch.lua",
        switch_lua.replace("@GATE_FILE@", GATE_FILE).replace("@ATTEMPTS@", ATTEMPTS),
    )

    os.makedirs(SHARED_DIR)
    os.chmod(SHARED_DIR, 0o777)

    atf.require_slurm_running()


@pytest.fixture(scope="function")
def gate():
    """Hold the script's hooks until the test lets them go."""

    path = pathlib.Path(GATE_FILE)
    path.touch()
    os.chmod(GATE_FILE, 0o666)

    yield lambda: path.unlink(missing_ok=True)

    path.unlink(missing_ok=True)


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


def teardown_attempts(job_id):
    """Return how many times slurm_switch_job_complete() ran for the job."""

    return len(atf.run_command_output(f"cat {ATTEMPTS}.{job_id}", quiet=True).split())


def test_nodes_are_released_while_the_teardown_runs(gate, resume_drained_nodes):
    """Test that the nodes are released while the teardown runs."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}hold --wrap "true"',
        fatal=True,
    )

    # Within the deadline, as a timed out teardown also completes the job.
    assert atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=SWITCH_TIMEOUT, fatal=True
    ), f"Job {job_id} did not complete while its teardown was still running"

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"{node} was held back by a teardown that said it need not be"

    assert not atf.wait_for_node_state(
        node, "COMPLETING", timeout=RELEASE_PROOF
    ), f"{node} went back to COMPLETING after being released"

    gate()


def test_failed_teardown_still_drains(resume_drained_nodes):
    """Test that a failed non-blocking teardown drains the allocation."""

    nodes = atf.run_job_nodes(
        f"-N{NODE_COUNT} --network={DIRECTIVE_PREFIX}fail true", fatal=True
    )
    assert len(nodes) == NODE_COUNT, f"Expected {NODE_COUNT} nodes, got {nodes}"

    # Within the deadline, as a timed out teardown drains with the same reason.
    assert atf.wait_for_node_state(
        nodes, "DRAIN", timeout=REPORT_WAIT, fatal=True
    ), f"{nodes} were not drained after a non-blocking teardown reported failure"

    for node in nodes:
        reason = atf.get_node_parameter(node, "reason") or ""
        assert (
            DRAIN_REASON in reason
        ), f"{node} was drained with an unexpected reason: {reason!r}"


def test_retry_request_without_a_hold_still_drains(resume_drained_nodes):
    """Test that a non-blocking teardown asking for a retry drains."""

    nodes = atf.run_job_nodes(
        f"-N{NODE_COUNT} --network={DIRECTIVE_PREFIX}eagain true", fatal=True
    )
    assert len(nodes) == NODE_COUNT, f"Expected {NODE_COUNT} nodes, got {nodes}"

    # Within the deadline, as above.
    assert atf.wait_for_node_state(
        nodes, "DRAIN", timeout=REPORT_WAIT, fatal=True
    ), f"{nodes} were not drained after a non-blocking teardown asked to be retried"

    for node in nodes:
        reason = atf.get_node_parameter(node, "reason") or ""
        assert (
            DRAIN_REASON in reason
        ), f"{node} was drained with an unexpected reason: {reason!r}"


def test_job_ended_during_setup_holds_its_nodes(gate, resume_drained_nodes):
    """Test that a job ended during setup holds its nodes until torn down."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}start_hold_retry --wrap "true"',
        fatal=True,
    )

    # The deadline is running; the gate must be dropped within it.
    assert atf.wait_for_job_state(
        job_id, "CONFIGURING", poll_interval=0.2, fatal=True
    ), f"Job {job_id} never reached CONFIGURING while job_start was running"

    atf.run_command(f"scancel {job_id}", fatal=True)

    assert atf.wait_for_node_state(
        node, "COMPLETING", fatal=True
    ), f"{node} was not held for the setup of job {job_id}, which had ended"

    assert not atf.wait_for_node_state(
        node, "COMPLETING", timeout=HOLD_PROOF, poll_interval=0.5, reverse=True
    ), f"{node} was released while the setup of job {job_id} was still running"

    gate()

    # The retry comes at the next SWITCH_TICK. The node is read before the
    # count, as the second call is counted before it can release the node.
    for _ in atf.timer(timeout=SWITCH_TICK + 20, poll_interval=1):
        states = atf.get_nodes(fatal=True, quiet=True)[node]["state"]
        if teardown_attempts(job_id) >= 2:
            break
        assert "COMPLETING" in states, (
            f"{node} was released before the teardown of job {job_id} was"
            f" done: {states}"
        )
    else:
        pytest.fail(
            f"slurm_switch_job_complete() was not called again for job {job_id}"
            f" after it returned slurm.EAGAIN ({teardown_attempts(job_id)} calls)"
        )

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(node, "IDLE", operator=atf.SetMatch.EQUAL), (
        f"{node} was not released, or was drained, after the retried teardown"
        f" of job {job_id}: {atf.get_nodes(fatal=True, quiet=True)[node]['state']}"
    )

    assert (
        teardown_attempts(job_id) == 2
    ), "slurm_switch_job_complete() was called more often than it asked for"


def test_teardown_that_never_reports_still_drains(gate, resume_drained_nodes):
    """Test that a non-blocking teardown that times out drains."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}timeout --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"Job {job_id} did not complete while its teardown was still running"

    assert atf.wait_for_node_state(
        node,
        "DRAIN",
        fatal=True,
        timeout=SWITCH_TIMEOUT + SWITCH_TICK + 20,
    ), (
        f"{node} was not drained after a non-blocking teardown failed to report"
        f" within switch_timeout={SWITCH_TIMEOUT}"
    )

    reason = atf.get_node_parameter(node, "reason") or ""
    assert (
        DRAIN_REASON in reason
    ), f"{node} was drained with an unexpected reason: {reason!r}"
