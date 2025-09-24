############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf
import time


def _all_jobs_of_state(job_ids=[], desired="RUNNING", timeout=60):
    while True:
        jobs = atf.get_jobs()
        states = [
            jobs[job_id]["JobState"]
            for job_id in jobs
            if not job_ids or job_id in job_ids
        ]
        if all(st == desired for st in states):
            return True
        timeout -= 1
        if timeout <= 0:
            break
        time.sleep(1)
    return False


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Needs to create/reconfigure nodes and partitions")
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def partition_nodes():
    """Create 2 partitions sharing the same node"""

    nodes = list(atf.get_nodes().keys())
    shared_node = nodes[0]

    atf.run_command(
        f"scontrol create PartitionName=p1 Nodes={shared_node} OverSubscribe=YES",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol create PartitionName=p2 Nodes={shared_node} OverSubscribe=NO",
        fatal=True,
        user=atf.properties["slurm-user"],
    )

    yield nodes

    atf.cancel_all_jobs()
    atf.run_command(
        "scontrol delete PartitionName=p1",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        "scontrol delete PartitionName=p2",
        user=atf.properties["slurm-user"],
    )


def test_overlapping_oversubscribe_differ_sharing_restriction_1(partition_nodes):
    """
    With the select plugin targeting CPU, two partitions sharing a node
    should be able to oversubscribe as long as the other partition does not
    have jobs on the shared node.
    """
    num_oversub_jobs = 2

    # Submit first job to p2 *with* --oversubscribe (despite OverSubscribe=NO)
    job_ids = [
        atf.submit_job_sbatch("-p p2 --oversubscribe --wrap='sleep 60'", fatal=True)
        for _ in range(num_oversub_jobs)
    ]

    # Jobs should run simultaneously
    assert _all_jobs_of_state(job_ids, timeout=60)

    atf.cancel_jobs(job_ids, fatal=True)

    # Submit a job to p1 *without* --oversubscribe
    job_ids = [atf.submit_job_sbatch("-p p1 --wrap='sleep 0'", fatal=True)]

    # Job should complete quickly
    atf.wait_for_job_state(job_ids[0], "COMPLETED", fatal=True, timeout=5)

    # Once again, submit job with oversubscribe to p2 now that p1's job is complete
    job_ids = [
        atf.submit_job_sbatch("-p p2 --oversubscribe --wrap='sleep 60'", fatal=True)
        for _ in range(num_oversub_jobs)
    ]

    # Verify last 'num_oversub_jobs' jobs are running
    assert _all_jobs_of_state(job_ids)
