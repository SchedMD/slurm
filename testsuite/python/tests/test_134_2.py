############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the switch/lua hooks, jobinfo round trip and return codes."""

import os
import pathlib

import pytest

import atf

NODE_COUNT = 2

# Shorter than the program a "background" setup starts.
SWITCH_TIMEOUT = 10

SHARED_DIR = None
SPECIAL_STEPS_FILE = None

ENV_TOKEN = "SWITCHLUA_TOKEN"
ENV_STEP_JOB_ID = "SWITCHLUA_STEP_JOB_ID"

TOKEN_PREFIX = "jobinfo-"

DIRECTIVE_PREFIX = "switchlua:"

# A --network value that has the job set up on the default path.
MANAGED = "managed"

# The "switchlua:" token in --network selects each case's behavior.
switch_lua = f"""
-- Record a call of a slurmctld hook for the job.
local function record(hook, job)
    local f = io.open("@SHARED_DIR@/hooks." .. job.job_id, "a")

    if f ~= nil then
        f:write(hook .. "\\n")
        f:close()
    end
end

local function directive(network)
    if network == nil then
        return ""
    end

    return string.match(network, "{DIRECTIVE_PREFIX}([%w_]+)") or ""
end

function slurm_switch_job_start(job)
    local mode = directive(job.network)

    record("slurm_switch_job_start", job)

    if mode == "decline" then
        return slurm.SUCCESS, nil
    end

    -- Write to stdout and stderr, directly and from another program.
    if mode == "output" then
        io.write("io.write\\n")
        io.stderr:write("io.stderr:write\\n")
        os.execute("echo os.execute; echo os.execute >&2")
    end

    -- Start a program longer than switch_timeout and return while it runs.
    if mode == "background" then
        os.execute("sleep {3 * SWITCH_TIMEOUT} &")
    end

    return slurm.SUCCESS, {{
        token = "{TOKEN_PREFIX}" .. job.job_id,
        mode = mode,
    }}
end

function slurm_switch_job_complete(job)
    record("slurm_switch_job_complete", job)

    return slurm.SUCCESS
end

function slurm_switch_job_preinit(step, stepinfo)
    if stepinfo == nil then
        return slurm.SUCCESS
    end
    if stepinfo.mode == "preinit_fail" then
        return -1
    end
    if stepinfo.mode == "preinit_crash" then
        error("deliberate error from slurm_switch_job_preinit")
    end

    return slurm.SUCCESS
end

function slurm_switch_job_init(step, stepinfo)
    local env = {{ {ENV_STEP_JOB_ID} = tostring(step.job_id) }}

    if stepinfo ~= nil then
        env.{ENV_TOKEN} = tostring(stepinfo.token)
    end

    return slurm.SUCCESS, env
end

function slurm_switch_job_postfini(step, stepinfo)
    if stepinfo ~= nil and stepinfo.mode == "postfini_drain" then
        return -1
    end

    return slurm.SUCCESS
end

-- Record each call made by slurmctld (slurmstepd calls it too).
function slurm_switch_setup_special_steps()
    local f = io.open("/proc/self/cmdline", "r")
    local cmdline = ""

    if f ~= nil then
        cmdline = f:read("*a")
        f:close()
    end

    if string.find(cmdline, "slurmctld", 1, true) then
        f = io.open("@SPECIAL_STEPS_FILE@", "a")

        if f ~= nil then
            f:write("x\\n")
            f:close()
        end
    end

    return false
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, SPECIAL_STEPS_FILE

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    SPECIAL_STEPS_FILE = f"{SHARED_DIR}/special_steps"

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
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("switch_timeout", SWITCH_TIMEOUT)
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file(
        "switch.lua",
        switch_lua.replace("@SPECIAL_STEPS_FILE@", SPECIAL_STEPS_FILE).replace(
            "@SHARED_DIR@", SHARED_DIR
        ),
    )

    os.makedirs(SHARED_DIR)
    os.chmod(SHARED_DIR, 0o777)
    pathlib.Path(SPECIAL_STEPS_FILE).touch()
    os.chmod(SPECIAL_STEPS_FILE, 0o666)

    atf.require_slurm_running()


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


def hooks_called(job_id):
    """Return the slurmctld hooks called for the job, one per call."""

    return atf.run_command_output(
        f"cat {SHARED_DIR}/hooks.{job_id}", quiet=True
    ).split()


def test_jobinfo_reaches_the_step_environment():
    """Test that jobinfo from job_start reaches job_init."""

    output = "token.out"
    job_id = atf.submit_job_sbatch(
        f'-N1 --network={MANAGED} --output={output} --wrap "srun printenv {ENV_TOKEN}"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"job {job_id} did not complete"

    atf.assert_file_contents(
        output,
        f"{TOKEN_PREFIX}{job_id}",
        message=(
            f"{ENV_TOKEN} did not carry the job id slurm_switch_job_start()"
            " put in the jobinfo dictionary, so the dictionary did not reach"
            " slurm_switch_job_init()"
        ),
    )

    assert "slurm_switch_job_start" in hooks_called(
        job_id
    ), f"slurm_switch_job_start() was not recorded for job {job_id}"


def test_step_record_is_visible_to_job_init():
    """Test that job_init sees the step record."""

    output = "step.out"
    job_id = atf.submit_job_sbatch(
        f"-N1 --network={MANAGED} --output={output}"
        f' --wrap "srun printenv {ENV_STEP_JOB_ID}"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"job {job_id} did not complete"

    atf.assert_file_contents(
        output,
        str(job_id),
        message=(
            f"{ENV_STEP_JOB_ID} did not match the job id, so step.job_id was"
            " not readable from slurm_switch_job_init()"
        ),
    )


def test_batch_step_does_not_run_job_init():
    """Test that job_init does not run for the batch step by default."""

    output = "batch.out"
    job_id = atf.submit_job_sbatch(
        f"-N1 --network={MANAGED} --output={output}"
        f" --wrap 'printenv {ENV_TOKEN} {ENV_STEP_JOB_ID}; echo rc=$?'",
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"job {job_id} did not complete"

    atf.assert_file_contents(
        output,
        "rc=1",
        message=(
            f"The batch step of job {job_id} got {ENV_TOKEN} or"
            f" {ENV_STEP_JOB_ID}, so slurm_switch_job_init() ran for it although"
            " slurm_switch_setup_special_steps() returned false"
        ),
    )


def test_declined_job_gets_no_stepinfo():
    """Test that a nil jobinfo from job_start leaves the job unmanaged."""

    result = atf.run_job(
        f"-N1 --network=switchlua:decline sh -c 'printenv {ENV_TOKEN}; echo rc=$?'",
        fatal=False,
    )

    assert result["exit_code"] == 0, (
        "A job slurm_switch_job_start() declined did not run"
        f" (stderr {result['stderr']!r})"
    )
    assert TOKEN_PREFIX not in result["stdout"], (
        f"{ENV_TOKEN} was set for a job slurm_switch_job_start() declined"
        f" (stdout {result['stdout']!r})"
    )
    assert (
        "rc=1" in result["stdout"]
    ), f"printenv did not report {ENV_TOKEN} unset (stdout {result['stdout']!r})"


def test_job_without_network_is_not_set_up():
    """Test that a job without --network runs its steps with no setup."""

    output = "unmanaged.out"
    job_id = atf.submit_job_sbatch(
        f"-N1 --output={output}"
        f" --wrap \"srun sh -c 'printenv {ENV_STEP_JOB_ID} {ENV_TOKEN}; true'\"",
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"job {job_id} did not complete"

    # printenv prints only the variables that are set.
    atf.assert_file_contents(
        output,
        str(job_id),
        message=(
            f"The srun step of job {job_id} did not get {ENV_STEP_JOB_ID} alone"
            " from slurm_switch_job_init() with nil stepinfo"
        ),
    )

    # A teardown starts as the job ends, so a short wait suffices.
    for t in atf.timer(timeout=2, xfail=True):
        if hooks_called(job_id):
            pytest.fail(
                f"slurmctld called {hooks_called(job_id)} for job {job_id} without --network"
            )


@pytest.mark.parametrize(
    "mode, what",
    [
        pytest.param("output", "wrote to stdout and stderr", id="output"),
        pytest.param("background", "left a program running", id="background"),
    ],
)
def test_job_start_output_and_programs_do_not_fail_the_setup(mode, what):
    """Test that job_start can write output and return while a program runs."""

    output = f"{mode}.out"
    job_id = atf.submit_job_sbatch(
        f"-N1 --no-requeue --network={DIRECTIVE_PREFIX}{mode} --output={output}"
        f' --wrap "srun printenv {ENV_TOKEN}"',
        fatal=True,
    )

    # A failed or timed out setup cancels a job that cannot be requeued.
    assert atf.wait_for_job_state(job_id, "COMPLETED"), (
        f"Job {job_id} did not complete after slurm_switch_job_start() {what}:"
        f" {atf.get_job_parameter(job_id, 'JobState')}"
        f" ({atf.get_job_parameter(job_id, 'Reason')})"
    )

    atf.assert_file_contents(
        output,
        f"{TOKEN_PREFIX}{job_id}",
        message=(
            f"{ENV_TOKEN} did not carry the jobinfo of job {job_id}, whose"
            f" slurm_switch_job_start() {what}"
        ),
    )


def test_preinit_failure_fails_the_step():
    """Test that a negative return from preinit fails the step."""

    result = atf.run_job("-N1 --network=switchlua:preinit_fail true", fatal=False)

    assert (
        result["exit_code"] != 0
    ), "The step succeeded despite slurm_switch_job_preinit() returning -1"

    # ESLURM_INTERCONNECT_FAILURE
    assert (
        "interconnect" in result["stderr"].lower()
    ), f"Expected an interconnect failure from srun, got {result['stderr']!r}"


def test_script_error_does_not_fail_the_step():
    """Test that a Lua error in preinit does not fail the step."""

    result = atf.run_job("-N1 --network=switchlua:preinit_crash true", fatal=False)

    assert result["exit_code"] == 0, (
        "An error raised inside slurm_switch_job_preinit() failed the step"
        f" (stderr {result['stderr']!r})"
    )


def test_postfini_failure_drains_the_node(resume_drained_nodes):
    """Test that a negative return from postfini drains the node."""

    nodes = atf.run_job_nodes("-N1 --network=switchlua:postfini_drain true", fatal=True)
    assert nodes, "Could not determine which node the step ran on"
    node = nodes[0]

    assert atf.wait_for_node_state(
        node, "DRAIN", fatal=True
    ), f"{node} was not drained after slurm_switch_job_postfini() returned -1"

    reason = atf.get_node_parameter(node, "reason") or ""
    assert (
        "switch_g_job_postfini failed" in reason
    ), f"{node} was drained with an unexpected reason: {reason!r}"


def special_steps_asked():
    """How many times slurmctld has called slurm_switch_setup_special_steps()."""

    output = atf.run_command_output(f"cat {SPECIAL_STEPS_FILE}", fatal=True, quiet=True)

    return len(output.split())


def test_special_steps_is_not_asked_for_each_launch():
    """Test that slurmctld calls setup_special_steps once, not per launch."""

    asked = special_steps_asked()

    for _ in range(2):
        job_id = atf.submit_job_sbatch('-N1 --wrap "true"', fatal=True)

        assert atf.wait_for_job_state(
            job_id, "COMPLETED", fatal=True
        ), f"job {job_id} did not complete"

    after = special_steps_asked()

    assert after > 0, (
        f"{SPECIAL_STEPS_FILE} records no call from slurmctld, not even when it"
        " loaded the plugin, so this test cannot see whether it asks the script"
    )
    assert after == asked, (
        f"slurmctld called slurm_switch_setup_special_steps() {asked} times"
        f" before two batch jobs and {after} after, so it asks the script on"
        " every launch"
    )
