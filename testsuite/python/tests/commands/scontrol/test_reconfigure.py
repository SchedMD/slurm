############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

# Setup/Teardown
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()

@pytest.fixture(scope='module')
def batch_job():
    """Submit a batch job and wait for it to start running"""
    job_id = atf.submit_job(fatal=True)
    atf.wait_for_job_state(job_id, 'RUNNING', fatal=True)
    return job_id

def test_reconfigure_status(batch_job):
    """Verify that scontrol reconfigure returns successfully"""
    exit_code = atf.run_command_exit("scontrol reconfigure", user=atf.properties['slurm-user'])
    assert exit_code == 0

def test_job_still_running(batch_job):
    """Verify that the job is still running after scontrol reconfigure"""
    job_state = atf.get_job_parameter(batch_job, 'JobState')
    assert job_state == 'RUNNING'
