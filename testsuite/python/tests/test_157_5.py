############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job_submit/lua reading state_reason and state_desc of a job record."""

import pytest

import atf

# On modify, copy the state reason and description of the job record into
# the comment, where scontrol shows them, when the comment asks for it. A
# slash joins them, as the comment is read back as one space-free value.
job_submit_lua = """
function slurm_job_submit(job_desc, part_list, submit_uid)
    return slurm.SUCCESS
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
    if job_desc.comment == "show_state" then
        job_desc.comment = string.format("%s/%s",
            tostring(job_rec.state_reason), tostring(job_rec.state_desc))
    end
    return slurm.SUCCESS
end
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51114: the job record exposes state_reason and state_desc to lua in 26.11+",
    )
    # job_submit/lua is only built with lua support
    atf.require_build_config("HAVE_LUA")
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_file("job_submit.lua", job_submit_lua)
    atf.require_slurm_running()


def show_state(job_id, user=None):
    """Return the "reason/description" slurm_job_modify() read."""
    atf.run_command(
        f"scontrol update JobId={job_id} Comment=show_state", user=user, fatal=True
    )
    return atf.get_job_parameter(job_id, "Comment", fatal=True)


def test_state_reason_and_desc():
    """slurm_job_modify() reads the reason a job waits for and its change.

    A job waiting for its begin time reports BeginTime, and once an
    administrator holds it JobHeldAdmin. A job record without a state
    description gives nil, as for any unset string field of the record.
    """
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    assert (
        show_state(job_id) == "BeginTime/nil"
    ), "A job waiting for its begin time should read BeginTime"

    atf.run_command(
        f"scontrol hold {job_id}", user=atf.properties["slurm-user"], fatal=True
    )
    assert (
        show_state(job_id, user=atf.properties["slurm-user"]) == "JobHeldAdmin/nil"
    ), "A job held by an administrator should read JobHeldAdmin"
