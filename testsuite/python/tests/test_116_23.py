############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import logging

import pexpect
import pytest

import atf

suser = atf.properties["slurm-user"]


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_single_job():
    """Submit job directly to slurmd without use of slurmctld scheduler."""

    node_dict = atf.get_nodes()
    node = list(node_dict.keys())[0]
    job_output = atf.run_job_output(
        f"-N1 --nodelist={node} --no-allocate printenv SLURMD_NODENAME", user=suser
    )
    assert (
        job_output.strip("\n") == node
    ), f"The job failed to print out the node name: {node}"


def test_multiple_jobs():
    """
    Run three tasks at a time on some node and do so repeatedly
    This checks for slurmd race conditions
    logic has time to be processed (slurmd -> slurmctld messages)
    Note: process output in order of expected completion
    """

    node_dict = atf.get_nodes()
    node = list(node_dict.keys())[0]
    iterations = 100
    for it in range(iterations):
        logging.info(f"Running iteration {it} of {iterations}...")
        children = [
            pexpect.spawn(f"srun -N1 --nodelist={node} printenv SLURMD_NODENAME"),
            pexpect.spawn(
                f"sudo -u {suser} bash -lc \"srun -N1 --nodelist={node} -Z bash -c 'printenv SLURMD_NODENAME; sleep 0.5'\""
            ),
            pexpect.spawn(
                f"sudo -u {suser} bash -lc \"srun -N1 --nodelist={node} -Z bash -c 'printenv SLURMD_NODENAME; sleep 0.25'\""
            ),
        ]

        for n, child in enumerate(children, 1):
            child.expect(pexpect.EOF, timeout=atf.default_command_timeout)
            child.close()
            # Use errors="replace" to avoid potentially false UnicodeDecodeError
            output = child.before.decode(errors="replace")
            output_lines = [line.strip() for line in output.splitlines()]
            assert (
                node in output_lines
            ), f"Child {n} should report being run on '{node}' on iteration {it}; got: {output!r}"
            assert (
                child.exitstatus == 0
            ), f"Child {n} should end correctly on iteration {it}; got {child.exitstatus}"
