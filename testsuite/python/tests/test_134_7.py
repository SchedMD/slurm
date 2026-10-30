############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test a switch/lua teardown that asks to be retried."""

import os

import pytest

import atf

NODE_COUNT = 2

# Switch retries are acted on every PERIODIC_TIMEOUT.
SWITCH_TICK = 30

MIN_JOB_AGE = 10

SWITCH_RETRIES = 1

SHARED_DIR = None
ATTEMPTS = None

DRAIN_REASON = "Switch job complete failed"

DIRECTIVE_PREFIX = "switchlua:"

switch_lua = f"""
local ATTEMPTS = "@ATTEMPTS@"

local function directive(network)
    if network == nil then
        return ""
    end

    return string.match(network, "{DIRECTIVE_PREFIX}([%w_]+)") or ""
end

-- Record a job_complete call and return the number of calls so far.
local function attempt(job)
    local path = ATTEMPTS .. "." .. tostring(job.job_id)
    local n = 0
    local f = io.open(path, "r")

    if f ~= nil then
        n = tonumber(f:read("*l")) or 0
        f:close()
    end

    n = n + 1
    f = io.open(path, "w")
    f:write(n .. "\\n")
    f:close()

    return n
end

function slurm_switch_block_job_complete()
    return true
end

function slurm_switch_job_start(job)
    return slurm.SUCCESS, {{ mode = directive(job.network) }}
end

function slurm_switch_job_complete(job)
    local mode = directive(job.network)
    local n = attempt(job)
    local wanted = string.match(mode, "^retry(%d+)$")

    -- "retryN" succeeds on the Nth retry.
    if wanted ~= nil then
        if n <= tonumber(wanted) then
            return slurm.EAGAIN
        end

        return slurm.SUCCESS
    end

    if mode == "retryforever" then
        return slurm.EAGAIN
    end

    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, ATTEMPTS

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    ATTEMPTS = f"{SHARED_DIR}/attempts"

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
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("switch_retries", SWITCH_RETRIES)
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file("switch.lua", switch_lua.replace("@ATTEMPTS@", ATTEMPTS))

    os.makedirs(SHARED_DIR)
    os.chmod(SHARED_DIR, 0o777)

    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
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


def attempts(job_id):
    """Return how many times the teardown ran for the job."""

    count = atf.run_command_output(f"cat {ATTEMPTS}.{job_id}", quiet=True).strip()
    return int(count) if count.isdigit() else 0


def test_retry_until_it_succeeds():
    """Test that a teardown retried the maximum number of times succeeds."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node} --network={DIRECTIVE_PREFIX}retry{SWITCH_RETRIES}"
        ' --wrap "true"',
        fatal=True,
    )

    for t in atf.timer(fatal=True, poll_interval=0.5):
        if attempts(job_id) >= 1:
            break

    other_id = atf.submit_job_sbatch(f'-N1 -w {node} --wrap "true"', fatal=True)

    # Each retry comes at the next SWITCH_TICK. The node and the other job are
    # read before the count, as the last call is counted before it can release
    # the node.
    for t in atf.timer(
        timeout=SWITCH_RETRIES * (SWITCH_TICK + 5) + 30, poll_interval=1
    ):
        states = atf.get_nodes(quiet=True)[node]["state"]
        other_state = atf.get_job_parameter(
            other_id, "JobState", fatal=True, quiet=True
        )
        if attempts(job_id) >= SWITCH_RETRIES + 1:
            break
        assert "COMPLETING" in states, f"{node} was not held for the retry: {states}"
        assert not {"DRAIN", "DOWN", "FAIL"} & set(
            states
        ), f"{node} was taken out of service while the teardown was being retried"
        assert (
            other_state == "PENDING"
        ), f"Job {other_id} was given {node} while its teardown was being retried"
    else:
        pytest.fail(
            f"Expected {SWITCH_RETRIES + 1} calls to the teardown of job {job_id},"
            f" got {attempts(job_id)}"
        )

    # The record may already be purged, which also means it was not stranded.
    for t in atf.timer(
        timeout=SWITCH_RETRIES * (SWITCH_TICK + 5) + 30, poll_interval=1
    ):
        state = atf.get_jobs(fatal=True, quiet=True).get(job_id, {}).get("JobState")
        if state in (None, "COMPLETED"):
            break
    else:
        pytest.fail(f"Job {job_id} was stranded by a teardown being retried: {state}")

    assert (
        attempts(job_id) == SWITCH_RETRIES + 1
    ), f"Expected {SWITCH_RETRIES + 1} calls to the teardown, got {attempts(job_id)}"

    assert atf.wait_for_job_state(
        other_id, "COMPLETED", fatal=True
    ), f"Job {other_id} never got {node} once it was released"
    # Exact, as a held node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", fatal=True, operator=atf.SetMatch.EQUAL
    ), f"{node} did not come back after its teardown succeeded on a retry"


def test_retries_run_out_and_drain():
    """Test that a teardown that never succeeds drains once retries run out."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}retryforever --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_node_state(
        node,
        "DRAIN",
        fatal=True,
        timeout=SWITCH_RETRIES * (SWITCH_TICK + 5) + 30,
    ), f"{node} was not drained after its teardown used up every retry"

    assert (
        attempts(job_id) == SWITCH_RETRIES + 1
    ), f"Expected {SWITCH_RETRIES + 1} calls to the teardown, got {attempts(job_id)}"

    reason = atf.get_node_parameter(node, "reason") or ""
    assert (
        DRAIN_REASON in reason
    ), f"{node} was drained with an unexpected reason: {reason!r}"
