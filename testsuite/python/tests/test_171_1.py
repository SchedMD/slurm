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
hook_marker = "runtime_lua_hook"
# A client-side hook fails when this, suffixed with its name, is set in the
# client's environment.
fail_prefix = "RUNTIME_LUA_FAIL_"

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

# The optional client-side hooks run in the submitting command itself, so each
# reports its arguments on that command's stderr.
hooks = [
    "apply_job_desc",
    "apply_step_req",
    "apply_launch_params",
    "apply_env",
    "apply_interactive_step",
]
for hook in hooks:
    runtime_lua += f"""
function slurm_runtime_{hook}(id, task_id)
    io.stderr:write("{hook_marker} {hook} " .. tostring(id) .. " "
                    .. tostring(task_id) .. "\\n")
    io.stderr:flush()
    if os.getenv("{fail_prefix}{hook}") then
        return slurm.ERROR
    end
    return slurm.SUCCESS
end
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    # --runtime is parsed by the client, and client commands are upgraded
    # after every daemon, so gating srun also covers slurmd and slurmctld.
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 50190: --runtime was added in 26.11"
    )
    # The plugin is only built with lua support (src/plugins/runtime/Makefile.am
    # guards the subdir with HAVE_LUA). Gate on what configure decided rather
    # than on the plugins srun lists, so a lua build missing runtime/lua fails.
    atf.require_build_config(
        "HAVE_LUA",
        reason="Issue 50190: Slurm was built without lua, so runtime/lua is absent",
    )
    atf.require_config_file("runtime.lua", runtime_lua)
    # salloc given no command runs its interactive step, and one that needs no
    # terminal is enough to show that it ran.
    atf.require_config_parameter_includes("LaunchParameters", "use_interactive_step")
    atf.require_config_parameter(
        "InteractiveStepOptions", f'"--interactive --preserve-env echo {task_marker}"'
    )
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


def _hook_calls(output):
    """Map each client-side hook that ran to the (id, task_id) of each call."""

    calls = {}
    for hook, id, task_id in re.findall(
        rf"^{hook_marker} (\S+) (\S+) (\S+)$", output, re.MULTILINE
    ):
        calls.setdefault(hook, []).append((id, task_id))
    return calls


def test_srun_client_hooks():
    """srun calls the job description, step request, launch and environment
    hooks with the ids it knows at each point.

    srun allocates the job itself here, so apply_job_desc runs before the job
    has an id. The step request is built for a known job, but before the step
    has an id, so apply_step_req gets the job id alone. The launch parameters
    and the tasks' environment carry no id at all. None of them is per-task.
    """

    result = atf.run_command("srun --runtime=lua -N1 printenv SLURM_JOB_ID")

    assert result["exit_code"] == 0, f"srun failed: {result['stderr']}"
    job_id = re.search(r"^(\d+)$", result["stdout"], re.MULTILINE)
    assert job_id, f"task printed no job id (stdout {result['stdout']!r})"
    calls = _hook_calls(result["stderr"])
    assert calls.get("apply_job_desc") == [
        ("nil", "nil")
    ], f"srun should call apply_job_desc once with no ids, got {calls}"
    assert calls.get("apply_step_req") == [
        (job_id.group(1), "nil")
    ], f"srun should call apply_step_req once with the job id, got {calls}"
    assert calls.get("apply_launch_params") == [
        ("nil", "nil")
    ], f"srun should call apply_launch_params once with no ids, got {calls}"
    assert calls.get("apply_env") == [
        ("nil", "nil")
    ], f"srun should call apply_env once for its tasks, got {calls}"


@pytest.mark.parametrize("command", ["sbatch", "salloc"])
def test_allocating_client_hooks(command):
    """sbatch and salloc call the job description hook once, before the job
    has an id, and the environment hook once, for the batch script or the
    command they run. They never call the step hooks, since they create no
    step themselves, nor the interactive step hook, which salloc calls for
    its interactive step alone."""

    if command == "sbatch":
        result = atf.run_command("sbatch --runtime=lua -N1 --wrap true")
    else:
        result = atf.run_command("salloc --runtime=lua -N1 true")

    assert result["exit_code"] == 0, f"{command} failed: {result['stderr']}"
    calls = _hook_calls(result["stderr"])
    assert calls.get("apply_job_desc") == [
        ("nil", "nil")
    ], f"{command} should call apply_job_desc once with no ids, got {calls}"
    assert calls.get("apply_env") == [
        ("nil", "nil")
    ], f"{command} should call apply_env once, got {calls}"
    assert (
        "apply_step_req" not in calls and "apply_launch_params" not in calls
    ), f"{command} creates no step, so should call no step hook, got {calls}"
    assert (
        "apply_interactive_step" not in calls
    ), f"{command} runs no interactive step, so should not call its hook, got {calls}"


def test_salloc_interactive_step_hooks():
    """salloc given no command runs its interactive step, and calls the
    interactive step hook once for it, besides the job description and
    environment hooks."""

    result = atf.run_command("salloc --runtime=lua -N1")

    assert result["exit_code"] == 0, f"salloc failed: {result['stderr']}"
    assert task_marker in result["stdout"], (
        "salloc never ran its interactive step"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )
    calls = _hook_calls(result["stderr"])
    for hook in ("apply_job_desc", "apply_env", "apply_interactive_step"):
        assert calls.get(hook) == [
            ("nil", "nil")
        ], f"salloc should call {hook} once with no ids, got {calls}"


def test_srun_launch_hook_failure_fails_launch():
    """A failing slurm_runtime_apply_launch_params() stops the launch.

    srun builds the launch parameters after the step is created, so the
    failure must still keep the task from ever running.
    """

    result = atf.run_command(
        f"srun --runtime=lua -N1 echo {task_marker}",
        env_vars=f"{fail_prefix}apply_launch_params=1",
        xfail=True,
    )

    assert result["exit_code"] != 0, "srun should fail when the launch hook fails"
    assert task_marker not in result["stdout"], (
        "the task ran although the launch hook failed"
        f" (stdout {result['stdout']!r}, stderr {result['stderr']!r})"
    )
