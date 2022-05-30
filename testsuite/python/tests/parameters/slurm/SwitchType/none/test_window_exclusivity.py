############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

check_iterations = 5
windows_used = 4
step_lifetime = 10

# Setup/Teardown
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_config_parameter('SwitchType', 'switch/none')
    atf.require_slurm_running()

@pytest.fixture(scope='module')
def salloc_noshell():
    """Submit a backgrounded salloc job"""
    job_id = atf.alloc_job_id("--verbose --no-shell", fatal=True)
    atf.wait_for_job_state(job_id, 'RUNNING', fatal=True)
    return job_id

@pytest.fixture(scope='module')
def srun_background(salloc_noshell):
    """Submit an initial job step to claim some switch windows"""
    return atf.run_command(f"nohup srun --jobid={salloc_noshell} -O -n {windows_used} sleep {step_lifetime} >/dev/null 2>&1 &", fatal=True)

def test_window_conflicts(salloc_noshell, srun_background):
    """Start more job steps to check see if any switch window conflicts occur"""
    for iteration in range(0, check_iterations):
        assert atf.run_job_exit(f"--jobid={salloc_noshell} -O -n {windows_used} true") == 0
