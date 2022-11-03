############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import pexpect

node_count = 2
slurm_user = atf.properties['slurm-user']


# Setup
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_config_parameter('SlurmdTimeout', 5, lambda v: v is not None and v <= 5, skip_message="This test requires SlurmdTimeout to be <= 5")
    atf.require_slurm_running()


@pytest.fixture(scope='module')
def no_kill_job():
    """Submit a job that should not be killed on node failure"""
    child = pexpect.spawn(f'srun --no-kill -N2 -v sleep 300')
    child.expect(r'jobid (\d+)', timeout=5)
    job_id = int(child.match.group(1))
    atf.wait_for_job_state(job_id, "RUNNING")
    # Grab the first allocated node before it is removed from the job so we can clean it up
    first_node = atf.node_range_to_list(atf.get_job_parameter(job_id, 'NodeList'))[0]

    yield job_id

    # Return the node to the idle state
    atf.run_command(f'scontrol update nodename={first_node} state=resume', user=slurm_user)


def test_no_kill(no_kill_job):
    """Verify job with --no-kill option is not killed on node failure"""

    job_nodes = atf.get_job_parameter(no_kill_job, 'NodeList')
    first_node = atf.node_range_to_list(job_nodes)[0]
    atf.run_command(f'scontrol update nodename={first_node} state=down reason=test_nokill', user=slurm_user)
    job_nodes_post_node_down = atf.get_job_parameter(no_kill_job, 'NodeList')
    assert job_nodes != job_nodes_post_node_down, "The job did not lose any nodes after a node was brought down"
    assert atf.get_job_parameter(no_kill_job, 'JobState') != 'COMPLETING', "The whole job was still canceled with --no-kill set"
