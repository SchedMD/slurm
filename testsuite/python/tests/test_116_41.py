############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_job_name():

    # Compare the job name given too the JobName field in scontrol
    job_name = "AAAAABBBBBCCCCCDDDDDEEEEEFFFFFGGGGGHHHHHIIIIIJJJJJKKKKKLLLLLMMMMM"
    job_id = atf.run_job_id(f"--job-name={job_name} true")
    job_param_name = atf.get_job_parameter(job_id, "JobName")
    assert job_name == job_param_name
