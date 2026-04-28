############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest


job_submit_lua = """
function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
    return slurm.SUCCESS
end

function slurm_job_submit(job_desc, part_list, submit_uid)
    if job_desc.core_spec == nil then
        slurm.log_user("core_spec is nil")
    else
        slurm.log_user("core_spec is %u", job_desc.core_spec)
    end

    if job_desc.thread_spec == nil then
        slurm.log_user("thread_spec is nil")
    else
        slurm.log_user("thread_spec is %u", job_desc.thread_spec)
    end

    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_parameter("AllowSpecResourcesUsage", "Yes")
    atf.require_config_file("job_submit.lua", job_submit_lua)
    # --core-spec=2 reserves 2 cores; need at least 3 to leave room for the job.
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5),
    reason="Ticket 25055: Expose core_spec/thread_spec to job_submit/lua, in 26.05+.",
)
@pytest.mark.parametrize(
    "cli_flag, core_exp, thread_exp",
    [
        ("", "nil", "nil"),
        ("--core-spec=2", "2", "nil"),
        ("--thread-spec=2", "nil", "2"),
    ],
)
def test_core_spec_thread_spec(cli_flag, core_exp, thread_exp):
    """Confirm core_spec / thread_spec exposure in the lua JobSubmitPlugin."""
    output = atf.run_command_error(f"srun {cli_flag} true", fatal=True)
    assert f"core_spec is {core_exp}" in output
    assert f"thread_spec is {thread_exp}" in output
