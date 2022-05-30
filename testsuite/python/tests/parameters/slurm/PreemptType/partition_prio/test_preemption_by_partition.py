############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import time

@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('PreemptType', 'preempt/partition_prio')
    atf.require_config_parameter('PreemptMode', 'CANCEL,GANG')
    atf.require_slurm_running()


@pytest.fixture(scope='module')
def partition_node():
    """Obtain a node that we can use in the partitions"""
    return atf.run_job_nodes("-N1 -t1 --exclusive true", fatal=True)[0]


@pytest.fixture(scope='module')
def partition1(partition_node):
    """Create partition 1"""
    partition_name = "partition1"
    atf.run_command(f"scontrol create partitionname={partition_name} nodes={partition_node} priority=1 preemptmode=cancel", user=atf.properties['slurm-user'], fatal=True)
    return partition_name


@pytest.fixture(scope='module')
def partition2(partition_node):
    """Create partition 2"""
    partition_name = "partition2"
    atf.run_command(f"scontrol create partitionname={partition_name} nodes={partition_node} priority=2 preemptmode=off", user=atf.properties['slurm-user'], fatal=True)
    return partition_name


@pytest.fixture(scope='function')
def cancel_jobs():
    """Cancel all jobs after each test"""
    yield
    atf.cancel_all_jobs()


def test_preempt_cancel(partition1, partition2, cancel_jobs):
    """Test preempt cancel"""

    job_id1 = atf.submit_job(f"-N1 -t1 -o /dev/null --exclusive -p {partition1} --wrap \"sleep 120\"", fatal=True)
    assert atf.wait_for_job_state(job_id1, 'RUNNING'), f"Job 1 ({job_id1}) did not start"
    job_id2 = atf.submit_job(f"-N1 -t1 -o /dev/null --exclusive -p {partition2} --wrap \"sleep 30\"", fatal=True)
    assert atf.wait_for_job_state(job_id2, 'RUNNING'), f"Job 2 ({job_id2}) did not start"
    assert atf.wait_for_job_state(job_id1, 'PREEMPTED'), f"Job 1 ({job_id1}) did not get preempted"


def test_preempt_suspend(partition1, partition2, cancel_jobs):
    """Test preempt suspend"""

    atf.run_command(f"scontrol update partitionname={partition1} preemptmode=suspend", user=atf.properties['slurm-user'], fatal=True)

    job_id1 = atf.submit_job(f"-N1 -t1 -o /dev/null --exclusive -p {partition1} --wrap \"sleep 120\"", fatal=True)
    assert atf.wait_for_job_state(job_id1, 'RUNNING'), f"Job 1 ({job_id1}) did not start"
    job_id2 = atf.submit_job(f"-N1 -t1 -o /dev/null --exclusive -p {partition2} --wrap \"sleep 30\"", fatal=True)
    assert atf.wait_for_job_state(job_id2, 'RUNNING'), f"Job 2 {job_id2} did not start"
    assert atf.wait_for_job_state(job_id1, 'SUSPENDED'), f"Job 1 ({job_id1}) did not get suspended"
    assert atf.wait_for_job_state(job_id2, 'DONE', timeout=60, poll_interval=1), f"Job 2 ({job_id2}) did not complete"
    assert atf.wait_for_job_state(job_id1, 'RUNNING'), f"Job 1 ({job_id1}) did not start running again"


def test_preempt_requeue(partition1, partition2, cancel_jobs):
    """Test preempt requeue"""

    atf.run_command(f"scontrol update partitionname={partition1} preemptmode=requeue", user=atf.properties['slurm-user'], fatal=True)

    job_id1 = atf.submit_job(f"-N1 -t1 -o /dev/null --exclusive -p {partition1} --wrap \"sleep 120\"", fatal=True)
    assert atf.wait_for_job_state(job_id1, 'RUNNING'), f"Job  1 ({job_id1}) did not start"
    job_id2 = atf.submit_job(f"-N1 -t1 -o /dev/null --exclusive -p {partition2} --wrap \"sleep 30\"", fatal=True)
    assert atf.wait_for_job_state(job_id2, 'RUNNING'), f"Job 2 ({job_id2}) did not start"
    assert atf.wait_for_job_state(job_id1, 'PENDING'), f"Job 1 ({job_id1}) did not return to pending"
    assert atf.wait_for_job_state(job_id2, 'DONE', timeout=60, poll_interval=1), f"Job 2 ({job_id2}) did not complete"
    assert atf.wait_for_job_state(job_id1, 'RUNNING', timeout=150, poll_interval=1), f"Job 1 ({job_id1}) did not start running again"
    time.sleep(5)
    assert atf.get_job_parameter(job_id1, 'JobState') == 'RUNNING', f"Job 1 ({job_id1}) was not still running after 5 seconds"
