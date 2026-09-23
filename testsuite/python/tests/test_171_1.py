############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that selecting runtime/lua does not break normal job launch.

A smoke test for the runtime/lua plugin: with a runtime.lua that accepts
everything, srun, sbatch and salloc must still run the task and produce its
output. slurm_runtime_run() returning slurm.SUCCESS means "I did not exec the
task", so slurmstepd execs it itself -- asserting on the task's own output is
what distinguishes that from a step that merely exited cleanly without ever
running anything. slurm_runtime_run() also prints its own marker, which only
appears if the script was actually called.
"""

import re

import pytest

import atf

script_marker = "runtime_lua_ran"
task_marker = "task_ran"

# Accepts every runtime operation and never execs the task itself, so
# slurmstepd launches it exactly as it would without the plugin. Matches the
# shape of etc/runtime.lua.example, minus its logging. run() prints
# script_marker on the task's stdout ahead of the task's own output.
runtime_lua = f"""
function slurm_runtime_setup(id)
    return slurm.SUCCESS
end

function slurm_runtime_cleanup(id)
    return slurm.SUCCESS
end

function slurm_runtime_task_init(id, task_id)
    return slurm.SUCCESS
end

function slurm_runtime_run(id, task_id)
    io.write("{script_marker}\\n")
    io.stdout:flush()
    return slurm.SUCCESS
end
"""

# The plugin is only built with lua support (src/plugins/runtime/Makefile.am
# guards the subdir with HAVE_LUA), so a build without lua has no
# runtime_lua.so to select. --runtime=list enumerates the installed runtime
# plugins on stderr, one short name per line.
pytestmark = pytest.mark.skipif(
    not re.search(
        r"^\s*lua\s*$",
        atf.run_command("srun --runtime=list", quiet=True)["stderr"],
        re.MULTILINE,
    ),
    reason="Issue 50190: Slurm was built without lua, so runtime/lua is absent",
)


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    # --runtime is parsed by the client, and client commands are upgraded
    # after every daemon, so gating srun also covers slurmd and slurmctld.
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 50190: --runtime was added in 26.11"
    )
    atf.require_config_file("runtime.lua", runtime_lua)
    atf.require_slurm_running()


def test_srun_runs_task():
    """srun --runtime=lua runs the task and returns its output."""

    result = atf.run_job(f"--runtime=lua -N1 echo {task_marker}")

    assert result["exit_code"] == 0, f"srun failed: {result['stderr']}"
    assert script_marker in result["stdout"], (
        "srun --runtime=lua never called slurm_runtime_run()"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )
    assert task_marker in result["stdout"], (
        "srun --runtime=lua produced no task output, so the task never ran"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )


def test_sbatch_runs_task():
    """sbatch --runtime=lua runs the batch script and completes."""

    output = "sbatch_171_1.out"
    job_id = atf.submit_job_sbatch(
        f"--runtime=lua -N1 --output={output} --wrap 'echo {task_marker}'",
        fatal=True,
    )

    assert atf.wait_for_job_state(job_id, "COMPLETED"), f"job {job_id} did not complete"
    atf.assert_file_contents(
        output,
        script_marker,
        contains=True,
        message=f"job {job_id} never called slurm_runtime_run()",
    )
    atf.assert_file_contents(
        output,
        task_marker,
        contains=True,
        message=f"job {job_id} completed but the batch script never ran",
    )


def test_salloc_runs_task():
    """An srun step with --runtime=lua inside an salloc allocation runs the task.

    --runtime has to be on the step that launches the task. salloc's own
    --runtime applies to the job and is exported as SLURM_RUNTIME, which srun
    does not read as an input option -- srun reads SRUN_RUNTIME -- so until
    Issue 51198 is fixed a normal step does not inherit the job's runtime.
    """

    result = atf.run_command(f"salloc -N1 srun --runtime=lua echo {task_marker}")

    assert result["exit_code"] == 0, f"salloc failed: {result['stderr']}"
    assert script_marker in result["stdout"], (
        "srun --runtime=lua under salloc never called slurm_runtime_run()"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )
    assert task_marker in result["stdout"], (
        "srun --runtime=lua under salloc produced no task output, so the task"
        f" never ran (stdout {result['stdout']!r},"
        f" stderr {result['stderr']!r})"
    )
