############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct names the user that cancelled a job or step.

slurmctld records the name of the user that asked for the cancel, which
sacct shows in the State column as "CANCELLED by <user>".
"""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Before 26.11, sacct shows the uid of the user instead of the name.
    # Remove this when 26.05 is no longer supported.
    atf.require_version(
        (26, 11), "bin/sacct", reason="sacct shows the canceller's name"
    )
    atf.require_accounting()
    # --stepmgr is silently dropped without it, see PrologFlags in slurm.conf(5)
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def job_owner(job_id):
    """Return the name of the user that owns the job, as accounting has it."""
    owner = ""
    for _ in atf.timer(fatal=True):
        owner = atf.run_command_output(f"sacct -nPX -j {job_id} -o user").strip()
        if owner:
            break
    return owner


def cancelled_state(sacct_args):
    """Wait for accounting to record the cancel, then return the State."""
    state = ""
    for _ in atf.timer(fatal=True):
        state = atf.run_command_output(f"sacct -nP {sacct_args} -o state%80").strip()
        if state.startswith("CANCELLED"):
            break
    return state


def test_owner_cancels_job():
    """The owner of a job is named when it cancels the job."""
    job_id = atf.submit_job_sbatch('--wrap "sleep infinity"', fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    owner = job_owner(job_id)

    atf.run_command(f"scancel {job_id}", fatal=True)

    state = cancelled_state(f"-X -j {job_id}")
    assert (
        state == f"CANCELLED by {owner}"
    ), f"Expected {owner} as the canceller, got {state!r}"


def test_other_user_cancels_job():
    """A user that cancels another user's job is named, not the owner."""
    job_id = atf.submit_job_sbatch('--wrap "sleep infinity"', fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    owner = job_owner(job_id)
    canceller = atf.properties["slurm-user"]
    if canceller == owner:
        canceller = "root"

    atf.run_command(f"scancel {job_id}", user=canceller, fatal=True)

    state = cancelled_state(f"-X -j {job_id}")
    assert (
        state == f"CANCELLED by {canceller}"
    ), f"Expected {canceller} as the canceller of {owner}'s job, got {state!r}"


def test_owner_cancels_step():
    """The owner of a job is named when it cancels one of the job's steps."""
    job_id = atf.submit_job_sbatch('--wrap "srun sleep infinity"', fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)
    owner = job_owner(job_id)

    atf.run_command(f"scancel {job_id}.0", fatal=True)

    state = cancelled_state(f"-j {job_id}.0")
    assert (
        state == f"CANCELLED by {owner}"
    ), f"Expected {owner} as the canceller, got {state!r}"


def test_owner_cancels_stepmgr_step():
    """The owner of a --stepmgr job is named when it cancels one of its steps.

    slurmstepd manages the steps of such a job, and slurmctld forwards the
    cancel to it through slurmd.
    """
    job_id = atf.submit_job_sbatch('--stepmgr --wrap "srun sleep infinity"', fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)
    assert (
        atf.get_job_parameter(job_id, "StepMgrEnabled") == "Yes"
    ), "The job should have a stepmgr"
    owner = job_owner(job_id)

    atf.run_command(f"scancel {job_id}.0", fatal=True)

    state = cancelled_state(f"-j {job_id}.0")
    assert (
        state == f"CANCELLED by {owner}"
    ), f"Expected {owner} as the canceller, got {state!r}"
