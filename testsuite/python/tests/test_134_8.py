############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test how switch/lua interprets the return values of its job hooks."""

import os

import pytest

import atf

# Longer than any wait here, so the deadline cannot end one.
SWITCH_TIMEOUT = 60

SHARED_DIR = None
MARKER_DIR = None

DRAIN_REASON = "Switch job complete failed"

DIRECTIVE_PREFIX = "switchlua:"

switch_lua = f"""
local MARKER_DIR = "@MARKER_DIR@"

local function directive(network)
    if network == nil then
        return ""
    end

    return string.match(network, "{DIRECTIVE_PREFIX}([%w_]+)") or ""
end

-- Record the directive a hook answers for the job.
local function record(hook, job, answered)
    local f = io.open(MARKER_DIR .. "/" .. hook .. "." .. job.job_id, "a")

    if f ~= nil then
        f:write(answered .. "\\n")
        f:close()
    end
end

local START = {{
    start_negative = function()
        return -2, nil
    end,
    start_slurm_error = function()
        return slurm.ERROR, nil
    end,
    start_positive = function()
        return 1, nil
    end,
    start_eagain = function()
        return slurm.EAGAIN, nil
    end,
    start_string = function()
        return "failed", nil
    end,
    start_nil_code = function(mode)
        return nil, {{ mode = mode }}
    end,
    start_no_value = function()
    end,
    start_true = function()
        return true, nil
    end,
    start_error = function()
        error("deliberate error from slurm_switch_job_start")
    end,
    start_fail_with_dictionary = function(mode)
        return slurm.ERROR, {{ mode = mode }}
    end,
}}

local COMPLETE = {{
    complete_negative = function()
        return -2
    end,
    complete_error = function()
        error("deliberate error from slurm_switch_job_complete")
    end,
    complete_positive = function()
        return 1
    end,
    complete_string = function()
        return "done"
    end,
    complete_no_value = function()
    end,
    complete_false = function()
        return false
    end,
    complete_table = function()
        return {{}}
    end,
}}

-- Hold the nodes until the teardown reports (not the default).
function slurm_switch_block_job_complete()
    return true
end

function slurm_switch_job_start(job)
    local mode = directive(job.network)
    local answer = START[mode]

    if answer == nil then
        record("start", job, "default")
        return slurm.SUCCESS, {{ mode = mode }}
    end

    record("start", job, mode)
    return answer(mode)
end

function slurm_switch_job_complete(job)
    local mode = directive(job.network)
    local answer = COMPLETE[mode]

    if answer == nil then
        record("complete", job, "default")
        return slurm.SUCCESS
    end

    record("complete", job, mode)
    return answer()
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, MARKER_DIR

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    MARKER_DIR = f"{SHARED_DIR}/markers"

    atf.require_version(
        (26, 11), "sbin/slurmd", reason="Issue 51019: switch/lua was added in 26.11"
    )
    atf.require_build_config(
        "HAVE_LUA",
        reason="Issue 51019: Slurm was built without lua, so switch/lua is absent",
    )
    atf.require_config_parameter("SwitchType", "switch/lua")
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("switch_timeout", SWITCH_TIMEOUT)
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file(
        "switch.lua", switch_lua.replace("@MARKER_DIR@", MARKER_DIR)
    )

    os.makedirs(MARKER_DIR)
    os.chmod(SHARED_DIR, 0o777)
    os.chmod(MARKER_DIR, 0o777)

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


def answers(hook, job_id):
    """Return the directives a hook answered for the job, one per call."""

    return atf.run_command_output(
        f"cat {MARKER_DIR}/{hook}.{job_id}", quiet=True
    ).split()


@pytest.mark.parametrize(
    "mode",
    [
        "start_negative",
        "start_slurm_error",
        "start_positive",
        "start_eagain",
        "start_string",
        "start_nil_code",
        "start_no_value",
        "start_true",
        "start_error",
        "start_fail_with_dictionary",
    ],
)
def test_job_start_failure_cancels_the_job_with_no_teardown(mode):
    """Test that anything but slurm.SUCCESS from job_start fails the setup."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --no-requeue --network={DIRECTIVE_PREFIX}{mode} --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "CANCELLED", desired_reason="switch_setup_failed", fatal=True
    ), f"Job {job_id} was not cancelled for a failed setup after {mode}"

    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"{node} was held back or drained for a setup that failed with {mode}"

    assert answers("start", job_id) == [mode], (
        f"slurm_switch_job_start() answered {answers('start', job_id)} for"
        f" job {job_id}, expected exactly one {mode}"
    )
    assert answers("complete", job_id) == [], (
        f"slurm_switch_job_complete() ran for job {job_id}, whose setup failed"
        f" with {mode}"
    )


@pytest.mark.parametrize(
    "mode",
    [
        "complete_positive",
        "complete_string",
        "complete_no_value",
        "complete_false",
        "complete_table",
    ],
)
def test_job_complete_answer_that_is_not_a_failure_releases_the_node(mode):
    """Test that a non-failure job_complete answer releases the node."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}{mode} --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"Job {job_id} stayed completing after its teardown answered {mode}"

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"{node} was not released undrained after its teardown answered {mode}"

    assert answers("complete", job_id) == [mode], (
        f"slurm_switch_job_complete() answered {answers('complete', job_id)}"
        f" for job {job_id}, expected exactly one {mode}"
    )


@pytest.mark.parametrize("mode", ["complete_negative", "complete_error"])
def test_job_complete_failure_drains_the_node(mode):
    """Test that a negative number or Lua error from job_complete drains."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}{mode} --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_node_state(
        node, "DRAIN", fatal=True
    ), f"{node} was not drained after its teardown answered {mode}"

    reason = atf.get_node_parameter(node, "reason") or ""
    assert (
        DRAIN_REASON in reason
    ), f"{node} was drained with an unexpected reason: {reason!r}"

    assert answers("complete", job_id) == [mode], (
        f"slurm_switch_job_complete() answered {answers('complete', job_id)}"
        f" for job {job_id}, expected exactly one {mode}"
    )
