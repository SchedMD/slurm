############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import pexpect
import time

suser = atf.properties['slurm-user']


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_single_job():
    """Submit job directly to slurmd without use of slurmctld scheduler."""

    node_dict = atf.get_nodes()
    node = list(node_dict.keys())[0]
    job_output = atf.run_job_output(f"-N1 --nodelist={node} --no-allocate printenv SLURMD_NODENAME", user=suser)
    assert job_output.strip('\n') == node, f"The job failed to print out the node name: {node}"


def test_multiple_jobs():
    """
    Run three tasks at a time on some node and do so repeatedly
    This checks for slurmd race conditions
    The sleep between cycles is to make sure the job step completion
    logic has time to be processed (slurmd -> slurmctld messages)
    Note: process output in order of expected completion
    """

    node_dict = atf.get_nodes()
    node = list(node_dict.keys())[0]
    for it in range(100):
        child1 = pexpect.spawn(f"srun -N1 --nodelist={node} true")
        child2 = pexpect.spawn(f'sudo -u {suser} bash -lc "srun -N1 --nodelist={node} -Z sleep 0.5"')
        child3 = pexpect.spawn(f'sudo -u {suser} bash -lc "srun -N1 --nodelist={node} -Z sleep 0.25"')

        pattern_index = child2.expect([r'error:.*configuring interconnect', r'error:', pexpect.EOF])
        assert pattern_index != 1, f'Child 2 failed to run on iteration {it}'
        pattern_index = child3.expect([r'error:.*configuring interconnect', r'error:', pexpect.EOF])
        assert pattern_index != 1, f'Child 3 failed to run on iteration {it}'
        pattern_index = child1.expect([r'error:.*configuring interconnect', r'error:', pexpect.EOF])
        assert pattern_index != 1, f'Child 1 failed to run on iteration {it}'
        time.sleep(0.25)
