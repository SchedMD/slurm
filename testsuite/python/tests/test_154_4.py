############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Basic life of a job using a hierarchical resource served by hres/lua.

A recording script writes one file per callback and job, which the test
checks for. Each callback gets its own file because hres_node_user_task_prolog
runs as the job's user and cannot write to a file another user created.
"""

import os

import pytest

import atf

HRES = "lualife"
SCRIPT = "hres_lifecycle.lua"
JOB_OPTS = "-J test_154_4 -t1 -o /dev/null"
PROLOG_TIMEOUT = 5
# Keeps a mode=async job CONFIGURING long enough to cancel it there.
ASYNC_DELAY = 15
node = None

LUA_SCRIPT = """
local function record(config, job, name, text)
    local f = io.open(config.record_dir .. "/" .. job.job_id .. "." .. name, "w")
    if f then
        f:write(text or "")
        f:close()
    end
end

function hres_create_context(ctx, layers)
    return slurm.SUCCESS
end

function hres_destroy_context(ctx)
    return slurm.SUCCESS
end

function hres_maintenance(ctx, layers)
    return slurm.SUCCESS
end

function hres_validate_args(ctx, args)
    if args.mode == "bad" then
        return slurm.ERROR
    end
    return slurm.SUCCESS
end

function hres_controller_job_prerun(ctx, job, prep)
    if prep.plugin_args.mode == "fail" then
        return slurm.ERROR
    end
    record(ctx.config, job, "controller_job_prerun")
    return slurm.SUCCESS, "token=" .. job.job_id
end

function hres_controller_job_async_prerun(ctx, job, prep)
    if prep.plugin_args.mode == "async" then
        os.execute("sleep " .. ctx.config.async_delay)
    end
    return slurm.SUCCESS
end

function hres_controller_job_revoke(ctx, job, prep)
    record(ctx.config, job, "controller_job_revoke")
    return slurm.SUCCESS
end

function hres_controller_job_epilog(ctx, job, prep)
    record(ctx.config, job, "controller_job_epilog",
           tostring(prep.async_running))
    return slurm.SUCCESS
end

function hres_node_job_prolog(prep, job)
    -- Only Lua instructions, so the PrologTimeout hook stops it.
    if prep.plugin_args.mode == "hang" then
        while true do end
    end
    record(prep.config, job, "node_job_prolog",
           (prep.payload or "") .. " " .. (prep.plugin_args.mode or ""))
    return slurm.SUCCESS
end

function hres_node_job_epilog(prep, job)
    record(prep.config, job, "node_job_epilog")
    return slurm.SUCCESS
end

function hres_node_task_prolog(prep, job, node_tid, global_tid)
    record(prep.config, job, "node_task_prolog")
    return slurm.SUCCESS
end

function hres_node_task_epilog(prep, job, node_tid, global_tid)
    record(prep.config, job, "node_task_epilog")
    return slurm.SUCCESS
end

function hres_node_user_task_prolog(prep, job, node_tid, global_tid)
    record(prep.config, job, "node_user_task_prolog")
    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="Issue #51036: HRES plugins were added in 26.11",
    )
    global node

    atf.require_build_config("HAVE_LUA", reason="hres/lua needs Lua")
    node = atf.require_nodes(1)[0]
    atf.require_config_parameter("PrologTimeout", PROLOG_TIMEOUT)
    # The node job epilog must run without an Epilog script too.
    atf.require_config_parameter("Epilog", None)

    atf.require_config_file(SCRIPT, LUA_SCRIPT)
    atf.require_config_file(
        "resources.yaml",
        f"""
- resource: {HRES}
  mode: MODE_1
  plugin:
    type: hres/lua
    config:
      - name: path
        value: {atf.properties['slurm-config-dir']}/{SCRIPT}
      - name: record_dir
        value: {atf.module_tmp_path}
      - name: async_delay
        value: "{ASYNC_DELAY}"
  layers:
    - layer_name: "all"
      nodes:
        - "{node}"
      count: 10
""",
    )
    atf.require_slurm_running()


def record_file(job_id, callback):
    return f"{atf.module_tmp_path}/{job_id}.{callback}"


def test_lifecycle():
    """The job callbacks run, and the payload and arguments reach the node."""

    job_id = atf.submit_job_sbatch(
        f'{JOB_OPTS} -L "{HRES}:1[mode=fast]" --wrap "true"', fatal=True
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    for callback in [
        "controller_job_prerun",
        "node_job_prolog",
        "node_task_prolog",
        "node_user_task_prolog",
        "node_task_epilog",
        "node_job_epilog",
        "controller_job_epilog",
    ]:
        assert atf.wait_for_file(
            record_file(job_id, callback)
        ), f"hres_{callback} should run for the job"

    atf.assert_file_contents(
        record_file(job_id, "node_job_prolog"),
        f"token={job_id} fast",
        message="The node should get the pre-run payload and the job's arguments",
    )
    atf.assert_file_contents(
        record_file(job_id, "controller_job_epilog"),
        "false",
        message="The controller epilog should see the async pre-run ended",
    )

    assert not os.path.exists(
        record_file(job_id, "controller_job_revoke")
    ), "A job that started should not be revoked"


def test_validate_args_rejected():
    """Arguments the script rejects fail the submission."""

    assert (
        atf.submit_job_sbatch(
            f'{JOB_OPTS} -L "{HRES}:1[mode=bad]" --wrap "true"',
            xfail=True,
            quiet=True,
        )
        == 0
    ), "Job should be rejected with arguments hres_validate_args refuses"


def test_update_args_rejected():
    """Arguments the script rejects fail a license update of a pending job."""

    job_id = atf.submit_job_sbatch(
        f'{JOB_OPTS} --hold -L "{HRES}:1[mode=fast]" --wrap "true"', fatal=True
    )
    assert (
        atf.run_command_exit(
            f"scontrol update jobid={job_id} Licenses='{HRES}:1[mode=bad]'",
            xfail=True,
            quiet=True,
        )
        != 0
    ), "The update should be rejected with arguments hres_validate_args refuses"
    assert (
        atf.get_job_parameter(job_id, "Licenses") == f"{HRES}:1[mode=fast]"
    ), "A rejected update should keep the job's licenses"

    atf.run_command(
        f"scontrol update jobid={job_id} Licenses='{HRES}:1[mode=slow]'",
        fatal=True,
    )
    assert (
        atf.get_job_parameter(job_id, "Licenses") == f"{HRES}:1[mode=slow]"
    ), "An update with arguments the script accepts should apply"


def test_prerun_failure():
    """A failed pre-run holds the job, which never reaches a node."""

    # Held, so the pre-run runs from the scheduler and not at submission,
    # where a failure rejects the job instead.
    job_id = atf.submit_job_sbatch(
        f'{JOB_OPTS} --hold -L "{HRES}:1[mode=fail]" --wrap "true"', fatal=True
    )
    atf.run_command(f"scontrol release {job_id}", fatal=True)

    for _ in atf.timer(fatal=True):
        reason = atf.get_job_parameter(job_id, "Reason", default="", quiet=True)
        if "HRES_pre-run_failed" in str(reason):
            break
    assert (
        atf.get_job_parameter(job_id, "JobState") == "PENDING"
    ), "A held job should stay pending"
    assert (
        int(atf.get_job_parameter(job_id, "Priority")) == 0
    ), "A failed pre-run should hold the job"

    assert not os.path.exists(
        record_file(job_id, "node_job_prolog")
    ), "A job whose pre-run failed should not reach a node"
    assert not os.path.exists(
        record_file(job_id, "controller_job_revoke")
    ), "A failed pre-run cleans up after itself, it is not revoked"


def test_async_running_in_epilog():
    """The controller epilog of a job cancelled while CONFIGURING sees async_running."""

    job_id = atf.submit_job_sbatch(
        f'{JOB_OPTS} -L "{HRES}:1[mode=async]" --wrap "true"', fatal=True
    )
    atf.wait_for_job_state(job_id, "CONFIGURING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    atf.assert_file_contents(
        record_file(job_id, "controller_job_epilog"),
        "true",
        message="The controller epilog should see the async pre-run still running",
    )


@pytest.fixture
def resume_node():
    yield
    atf.run_command(
        f"scontrol update nodename={node} state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


# Last, it drains the node.
def test_node_prolog_timeout(resume_node):
    """A node prolog stopped by PrologTimeout fails, drains the node and
    requeues the job held."""

    job_id = atf.submit_job_sbatch(
        f'{JOB_OPTS} -L "{HRES}:1[mode=hang]" --wrap "true"', fatal=True
    )
    # The drain waits for PrologTimeout, then for the prolog failure to reach
    # slurmctld, which can take longer than the default timeout.
    assert atf.wait_for_node_state(
        node, "DRAIN", timeout=PROLOG_TIMEOUT + 60
    ), "The node should be drained after its HRES prolog timed out"
    assert "Prolog error" in str(
        atf.get_node_parameter(node, "reason")
    ), "The node should be drained for its failed prolog"
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="launch_failed_requeued_held"
    ), "The job should be requeued and held after its prolog failed"
    assert (
        int(atf.get_job_parameter(job_id, "Restarts", default=0)) == 1
    ), "The job should be requeued once"
