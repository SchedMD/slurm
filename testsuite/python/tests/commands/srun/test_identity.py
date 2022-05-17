############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import os
import pytest
import re

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()

@pytest.fixture(scope='module')
def srun_output():
    return atf.run_command_output("srun -N 1 -t 1 id", fatal=True)

# Verify that a job executes as the appropriate user and group
def test_uid(srun_output):
    """Verify that a job executes as the invoking user"""
    test_uid = os.geteuid()
    job_uid = int(match.group(1)) if (match := re.search(r'uid=(\d+)', srun_output)) else None
    assert job_uid == test_uid

def test_gid(srun_output):
    """Verify that a job executes as the invoking group"""
    test_gid = os.getegid()
    job_gid = int(match.group(1)) if (match := re.search(r'gid=(\d+)', srun_output)) else None
    assert job_gid == test_gid
