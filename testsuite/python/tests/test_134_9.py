############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the switch/lua step functions when setup_special_steps returns true."""

import os

import pytest

import atf

MARKER_DIR = None

ENV_TOKEN = "SWITCHLUA_TOKEN"

TOKEN_PREFIX = "jobinfo-"

# A --network value that has the job set up.
MANAGED = "managed"

switch_lua = f"""
local MARKER_DIR = "@MARKER_DIR@"

-- Record one call of a step function for the job.
local function record(hook, step)
    local f = io.open(MARKER_DIR .. "/" .. hook .. "." .. step.job_id, "a")

    if f ~= nil then
        f:write("x\\n")
        f:close()
    end
end

function slurm_switch_job_start(job)
    return slurm.SUCCESS, {{ token = "{TOKEN_PREFIX}" .. job.job_id }}
end

function slurm_switch_job_preinit(step, stepinfo)
    record("preinit", step)
    return slurm.SUCCESS
end

function slurm_switch_job_init(step, stepinfo)
    record("init", step)

    if stepinfo == nil then
        return slurm.SUCCESS, nil
    end

    return slurm.SUCCESS, {{ {ENV_TOKEN} = tostring(stepinfo.token) }}
end

function slurm_switch_job_postfini(step, stepinfo)
    record("postfini", step)
    return slurm.SUCCESS
end

function slurm_switch_setup_special_steps()
    return true
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global MARKER_DIR

    MARKER_DIR = f"{atf.module_tmp_path}/markers"

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
    atf.require_config_parameter("SwitchType", "switch/lua")
    # Every job gets an extern step.
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file(
        "switch.lua", switch_lua.replace("@MARKER_DIR@", MARKER_DIR)
    )

    os.makedirs(MARKER_DIR)
    os.chmod(MARKER_DIR, 0o777)

    atf.require_slurm_running()


def calls(hook, job_id):
    """Return how many times a step function ran for the job."""

    return len(
        atf.run_command_output(f"cat {MARKER_DIR}/{hook}.{job_id}", quiet=True).split()
    )


def test_batch_step_runs_only_job_init():
    """Test that the batch step runs job_init alone and the extern step none."""

    output = "batch.out"
    job_id = atf.submit_job_sbatch(
        f'-N1 --network={MANAGED} --output={output} --wrap "printenv {ENV_TOKEN}"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "DONE", fatal=True
    ), f"job {job_id} did not finish"

    assert calls("init", job_id) == 1, (
        f"slurm_switch_job_init() ran {calls('init', job_id)} times for job"
        f" {job_id}, whose only steps are the batch and extern steps, expected"
        " once for the batch step"
    )
    assert calls("preinit", job_id) == 0, (
        f"slurm_switch_job_preinit() ran {calls('preinit', job_id)} times for"
        f" job {job_id}, which has no normal step"
    )
    assert calls("postfini", job_id) == 0, (
        f"slurm_switch_job_postfini() ran {calls('postfini', job_id)} times"
        f" for job {job_id}, which has no normal step"
    )

    atf.assert_file_contents(
        output,
        f"{TOKEN_PREFIX}{job_id}",
        message=(
            f"The batch step of job {job_id} did not get {ENV_TOKEN} from"
            " slurm_switch_job_init(), with the dictionary"
            " slurm_switch_job_start() returned"
        ),
    )


def test_srun_step_runs_every_step_function():
    """Test that preinit and postfini run for a normal step."""

    job_id = atf.submit_job_sbatch(
        f'-N1 --network={MANAGED} --wrap "srun true"', fatal=True
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"job {job_id} did not complete"

    assert calls("preinit", job_id) == 1, (
        f"slurm_switch_job_preinit() ran {calls('preinit', job_id)} times for"
        f" job {job_id}, expected once for its one srun step"
    )
    assert calls("postfini", job_id) == 1, (
        f"slurm_switch_job_postfini() ran {calls('postfini', job_id)} times for"
        f" job {job_id}, expected once for its one srun step"
    )
    assert calls("init", job_id) == 2, (
        f"slurm_switch_job_init() ran {calls('init', job_id)} times for job"
        f" {job_id}, expected once for its batch step and once for its srun step"
    )
