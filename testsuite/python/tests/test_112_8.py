############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test SLURM_JOB_NAME in the batch environment of slurmrestd jobs.

Jobs submitted through slurmrestd lost SLURM_JOB_NAME from their batch
script environment (Ticket 20333).
"""

import os

import pytest

import atf

# Print SLURM_JOB_NAME and its per-component variants, one per line
PRINT_JOB_NAMES = (
    "printenv | grep -E '^SLURM_JOB_NAME(_HET_GROUP_[0-9]+)?=' | LC_ALL=C sort"
)
# The job name must override any SLURM_JOB_NAME given by the client
ENV = ["PATH=/bin:/usr/bin", "SLURM_JOB_NAME=client_name"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((25, 5), "sbin/slurmrestd", max_version=(27, 5))
    atf.require_slurmrestd("slurmctld", "v0.0.43")
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_nodes(2)
    # Het jobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_slurm_running()

    atf.require_openapi_generator("7.3.0")


@pytest.fixture(scope="function")
def slurm(setup):
    yield atf.openapi_slurm()


def job_desc(name, environment=ENV):
    from openapi_client.models.v0043_job_desc_msg import V0043JobDescMsg

    return V0043JobDescMsg(
        name=name,
        environment=environment,
        current_working_directory=os.getcwd(),
        standard_output="job.out",
    )


def submit_job(slurm, **job):
    """Submits a batch job running PRINT_JOB_NAMES and waits for it to end."""
    from openapi_client.models.v0043_job_submit_req import V0043JobSubmitReq

    resp = slurm.slurm_v0043_post_job_submit(
        V0043JobSubmitReq(script=f"#!/bin/bash\n{PRINT_JOB_NAMES}\n", **job)
    )
    assert not resp.errors, f"Job submission should succeed: {resp.errors}"
    job_id = int(resp.job_id)
    atf.properties["submitted-jobs"].append(job_id)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)


@pytest.mark.parametrize("name", ["job_name", "job name with spaces"])
def test_job_name(slurm, name):
    """Verify SLURM_JOB_NAME is the job name, not the client's value"""
    submit_job(slurm, job=job_desc(name))
    atf.assert_file_contents("job.out", f"SLURM_JOB_NAME={name}")


def test_het_job_name(slurm):
    """Verify SLURM_JOB_NAME and its per-component variants for het jobs"""
    submit_job(slurm, jobs=[job_desc("het0_name"), job_desc("het1_name")])

    # The batch script gets the name of the first component
    atf.assert_file_contents(
        "job.out",
        "SLURM_JOB_NAME=het0_name\n"
        "SLURM_JOB_NAME_HET_GROUP_0=het0_name\n"
        "SLURM_JOB_NAME_HET_GROUP_1=het1_name",
    )


@pytest.mark.parametrize(
    "environment,expected",
    [(ENV, "SLURM_JOB_NAME=client_name"), (["PATH=/bin:/usr/bin"], "")],
    ids=["client_env", "no_client_env"],
)
def test_no_job_name(slurm, environment, expected):
    """Verify Slurm does not set SLURM_JOB_NAME when the job has no name"""
    submit_job(slurm, job=job_desc(None, environment=environment))
    atf.assert_file_contents("job.out", expected)
