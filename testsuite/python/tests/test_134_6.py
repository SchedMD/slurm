############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test SlurmctldParameters=switch_drain_on_failure=no."""

import os
import pathlib

import pytest

import atf

NODE_COUNT = 2

# Switch deadlines are acted on every PERIODIC_TIMEOUT.
SWITCH_TICK = 30

SWITCH_TIMEOUT = 5

MIN_JOB_AGE = 13

# How long a hold must last to not be a slow epilog; must fit inside
# SWITCH_TIMEOUT.
HOLD_PROOF = 2

# How long slurmctld may take to act on a report; short enough that the
# switch_timeout deadline cannot be what ends the wait.
REPORT_WAIT = 3

SHARED_DIR = None
GATE_FILE = None

DIRECTIVE_PREFIX = "switchlua:"

switch_lua = f"""
local GATE = "@GATE_FILE@"

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

local function directive(network)
    if network == nil then
        return ""
    end

    return string.match(network, "{DIRECTIVE_PREFIX}([%w_]+)") or ""
end

function slurm_switch_block_job_complete()
    return true
end

function slurm_switch_job_start(job)
    return slurm.SUCCESS, {{ mode = directive(job.network) }}
end

function slurm_switch_job_complete(job)
    local mode = directive(job.network)

    if mode == "timeout" then
        hold()
    end

    if mode == "fail" then
        return -1
    end

    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, GATE_FILE

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    GATE_FILE = f"{SHARED_DIR}/gate"

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
        "SlurmctldParameters", ("switch_drain_on_failure", "no")
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file("switch.lua", switch_lua.replace("@GATE_FILE@", GATE_FILE))

    os.makedirs(SHARED_DIR)
    os.chmod(SHARED_DIR, 0o777)

    atf.require_slurm_running()


@pytest.fixture(scope="function")
def gate():
    """Hold the teardown script until the test lets it go."""

    path = pathlib.Path(GATE_FILE)
    path.touch()
    os.chmod(GATE_FILE, 0o666)

    yield lambda: path.unlink(missing_ok=True)

    path.unlink(missing_ok=True)


@pytest.fixture(scope="function", autouse=True)
def nothing_was_drained():
    """Resume any drained node and fail if there was one."""

    yield

    drained = [
        node
        for node, node_dict in atf.get_nodes(quiet=True).items()
        if {"DOWN", "DRAIN", "FAIL"} & set(node_dict["state"])
    ]

    for node in drained:
        atf.run_command(
            f"scontrol update nodename={node} state=resume",
            user="root",
            quiet=True,
        )

    assert not drained, f"{drained} were drained despite switch_drain_on_failure=no"


def test_reported_failure_does_not_drain():
    """Test that a failed teardown does not drain."""

    nodes = atf.run_job_nodes(
        f"-N{NODE_COUNT} --network={DIRECTIVE_PREFIX}fail true", fatal=True
    )
    assert len(nodes) == NODE_COUNT, f"Expected {NODE_COUNT} nodes, got {nodes}"

    # Exact, as a held node also reports IDLE. Within the deadline, as a timed
    # out teardown also releases the nodes.
    assert atf.wait_for_node_state(
        nodes, "IDLE", timeout=REPORT_WAIT, fatal=True, operator=atf.SetMatch.EQUAL
    ), f"{nodes} did not come back after a teardown reported failure"


def test_silent_teardown_does_not_drain(gate):
    """Test that a teardown that times out does not drain."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}timeout --wrap "true"',
        fatal=True,
    )

    # The deadline is running; the hold check must end within it.
    assert atf.wait_for_job_state(
        job_id, "COMPLETING", poll_interval=0.2, fatal=True
    ), f"Job {job_id} never reached COMPLETING"

    assert not atf.wait_for_node_state(
        node, "COMPLETING", timeout=HOLD_PROOF, poll_interval=0.5, reverse=True
    ), f"{node} was released before switch_timeout={SWITCH_TIMEOUT} ran out"

    # Exact, as a held node also reports IDLE.
    assert atf.wait_for_node_state(
        node,
        "IDLE",
        timeout=SWITCH_TIMEOUT + SWITCH_TICK + 20,
        fatal=True,
        operator=atf.SetMatch.EQUAL,
    ), f"{node} did not come back after a teardown that never reported"

    # The record may already be purged, which also means it was not stranded.
    for t in atf.timer():
        jobs = atf.get_jobs(fatal=True, quiet=True)
        state = jobs.get(job_id, {}).get("JobState")
        if state in (None, "COMPLETED"):
            break
    else:
        pytest.fail(
            f"Job {job_id} was stranded by a teardown that was never drained"
            f" for: {state}"
        )
