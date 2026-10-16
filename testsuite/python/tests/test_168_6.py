############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the hold reason set on every component of a held heterogeneous job."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51114: the hold reason reaches every hetjob component in 26.11+",
    )
    # Holding one component holds the whole heterogeneous job
    atf.require_config_parameter_includes("SchedulerParameters", "whole_hetjob")
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "hold_command,admin,reason",
    [("uhold", False, "JobHeldUser"), ("hold", True, "JobHeldAdmin")],
    ids=["uhold", "admin_hold"],
)
def test_hold_one_component_sets_reason_on_all(hold_command, admin, reason):
    """A hold on one component shows its reason on every component.

    With whole_hetjob, holding a component zeroes the priority of every
    component of the job. Each of them must also report the reason of the
    hold: squeue only shows a held job for that reason, and it decides who
    may release it. The owner's uhold sets JobHeldUser, which the owner can
    release, and an administrator's hold JobHeldAdmin, which only an
    administrator can release.
    """
    user = atf.properties["slurm-user"] if admin else None
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null -N1 : -N1 --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    components = sorted(
        component_id
        for component_id, job in atf.get_jobs(job_id=job_id, fatal=True).items()
        if str(job.get("HetJobId")) == str(job_id)
    )
    assert (
        len(components) == 2
    ), f"Job {job_id} should show two components, got {components}"

    # Hold the component that is not the leader, not the job as a whole
    atf.run_command(f"scontrol {hold_command} {components[1]}", user=user, fatal=True)
    for component_id in components:
        job = atf.get_job(component_id)
        assert (
            job.get("Priority") == 0
        ), f"Component {component_id} should be held: Priority={job.get('Priority')}"
        assert (
            job.get("Reason") == reason
        ), f"Component {component_id} should report the hold: Reason={job.get('Reason')}"

    # Releasing a component releases the whole job as well
    atf.run_command(f"scontrol release {components[1]}", user=user, fatal=True)
    for component_id in components:
        job = atf.get_job(component_id)
        assert (
            job.get("Priority") > 0
        ), f"Component {component_id} should be released: Priority={job.get('Priority')}"
        assert (
            job.get("Reason") != reason
        ), f"Component {component_id} should no longer report the hold"
