############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Needs to create/reconfigure nodes and partitions")

    # Test needs 4 nodes to have 3 partitions with 1 overlaping
    # We want to test with mutiple sockets.
    # Partition will use up to MaxCPUsPerSocket=3, and we want that to be at least half of the
    # space so two partitions can share the same core.
    atf.require_nodes(4, [("Sockets", 2), ("CoresPerSocket", 6), ("ThreadsPerCore", 1)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def partition_nodes(limit_cpus, limit_name):
    """Create all 3 partitions with the 2 nodes and the desired limits and return
    the list of node names"""

    nodes = list(atf.get_nodes().keys())
    nodes_p1 = atf.node_list_to_range(nodes[0:2])
    nodes_p2 = atf.node_list_to_range(nodes[1:3])
    nodes_p3 = atf.node_list_to_range(nodes[2:4])

    atf.run_command(
        f"scontrol create PartitionName=p1 Nodes={nodes_p1} LLN=Yes MaxNodes=1 {limit_name}={limit_cpus}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol create PartitionName=p2 Nodes={nodes_p2} LLN=Yes MaxNodes=1 {limit_name}={limit_cpus}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol create PartitionName=p3 Nodes={nodes_p3} LLN=Yes MaxNodes=1 {limit_name}={limit_cpus}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )

    yield nodes

    atf.cancel_all_jobs()
    atf.run_command(
        f"scontrol delete PartitionName=p1",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol delete PartitionName=p3",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol delete PartitionName=p2",
        user=atf.properties["slurm-user"],
    )


@pytest.mark.parametrize("limit_name", ["MaxCPUsPerSocket", "MaxCPUsPerNode"])
@pytest.mark.parametrize("limit_cpus", [1, 2, 3])
def test_limits(limit_name, limit_cpus, partition_nodes):
    """Test that limit_name is honored also for overlaping partitions by
    incrementally submitting the max number of jobs that each partition can allocate
    based on the limits and assiming the necessary resources are available, and
    checking that limits are always honored, even when extra jobs are submitted
    at the end."""

    # Submit the maximum number of jobs per partition based on the limit set
    # For MaxCPUsPerNode is 2 nodes * limit_cpus per node
    # For MaxCPUsPerSocket is 2 nodes * limit_cpus per socket * 2 sockets per node
    max_jobs = limit_cpus * 2
    if limit_name == "MaxCPUsPerSocket":
        max_jobs *= 2

    # Submit max_jobs to the partition p1
    list_jobs = []
    for i in range(max_jobs):
        list_jobs.append(
            atf.submit_job_sbatch(f"-p p1 --wrap 'sleep infinity'", fatal=True)
        )
    for job_id in list_jobs:
        atf.wait_for_job_state(job_id, "RUNNING")

    # Verify that the number of allocated CPUs per node is correct, assuming 1 CPU per job
    # First two nodes should have jobs splitted between them due the limits
    assert (
        atf.get_node_parameter(partition_nodes[0], "CPUAlloc") == max_jobs / 2
    ), f"Verify that node {partition_nodes[0]} has {max_jobs/2} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[1], "CPUAlloc") == max_jobs / 2
    ), f"Verify that node {partition_nodes[1]} has {max_jobs/2} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[2], "CPUAlloc") == 0
    ), f"Verify that node {partition_nodes[2]} has 0 CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[3], "CPUAlloc") == 0
    ), f"Verify that node {partition_nodes[3]} has 0 CPUs allocated"

    # Submit max_jobs to the partition p2
    list_jobs = []
    for i in range(max_jobs):
        list_jobs.append(
            atf.submit_job_sbatch(f"-p p2 --wrap 'sleep infinity'", fatal=True)
        )
    for job_id in list_jobs:
        atf.wait_for_job_state(job_id, "RUNNING")

    # Verify that the number of allocated CPUs per node is incremented correctly
    # Second node is shared between p1 and p2, so should have half of the jobs of each partition.
    assert (
        atf.get_node_parameter(partition_nodes[0], "CPUAlloc") == max_jobs / 2
    ), f"Verify that node {partition_nodes[0]} has {max_jobs/2} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[1], "CPUAlloc") == max_jobs
    ), f"Verify that node {partition_nodes[1]} has {max_jobs} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[2], "CPUAlloc") == max_jobs / 2
    ), f"Verify that node {partition_nodes[2]} has {max_jobs/2} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[3], "CPUAlloc") == 0
    ), f"Verify that node {partition_nodes[3]} has 0 CPUs allocated"

    # Submit max_jobs to the last partition
    list_jobs = []
    for i in range(max_jobs):
        list_jobs.append(
            atf.submit_job_sbatch(f"-p p3 --wrap 'sleep infinity'", fatal=True)
        )
    for job_id in list_jobs:
        atf.wait_for_job_state(job_id, "RUNNING")

    # Verify that the number of allocated CPUs per node is incremented correctly
    # Third node is also shared, this case between p2 and p3.
    assert (
        atf.get_node_parameter(partition_nodes[0], "CPUAlloc") == max_jobs / 2
    ), f"Verify that node {partition_nodes[0]} has {max_jobs/2} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[1], "CPUAlloc") == max_jobs
    ), f"Verify that node {partition_nodes[1]} has {max_jobs} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[2], "CPUAlloc") == max_jobs
    ), f"Verify that node {partition_nodes[2]} has {max_jobs} CPUs allocated"
    assert (
        atf.get_node_parameter(partition_nodes[3], "CPUAlloc") == max_jobs / 2
    ), f"Verify that node {partition_nodes[3]} has 0 CPUs allocated"

    # Submit one more job in each partition and make sure we stay within the limits
    job_id = atf.submit_job_sbatch(f"-p p1 --wrap 'sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "PENDING", "Resources"
    ), f"Verify that job is not run in partition p1 but hold due resources"
    atf.cancel_jobs([job_id])

    job_id = atf.submit_job_sbatch(f"-p p2 --wrap 'sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "PENDING", "Resources"
    ), f"Verify that job is not run in partition p2 but hold due resources"
    atf.cancel_jobs([job_id])

    job_id = atf.submit_job_sbatch(f"-p p3 --wrap 'sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "PENDING", "Resources"
    ), f"Verify that job is not run in partition p3 but hold due resources"
    atf.cancel_jobs([job_id])


@pytest.mark.parametrize("limit_name", ["MaxCPUsPerSocket", "MaxCPUsPerNode"])
@pytest.mark.parametrize("limit_cpus", [0])
def test_zero_cpu(limit_name, partition_nodes):
    """Test the corener case of setting limit_name=0 means jobs cannot be submitted to that partition."""

    # This is an undocumented corner case and shouldn't be used.
    # Setting the partition down, drain or inactive should be used instead.
    # At the moment of writting this test the behavior between MaxCPUsPerSocket
    # and MaxCPUsPerNode is slightly different, but we don't really want to
    # enforce this exact current behavior but just to verify that jobs are rejected
    # or never run.

    job_id = atf.submit_job_sbatch(f"-p p1 --wrap 'sleep infinity'")
    assert job_id == 0 or not atf.wait_for_job_state(
        job_id, "RUNNING", xfail=True
    ), f"Verify that job is not run"
    atf.cancel_jobs([job_id])

    # This is not necessary and it's more an example than an actual test:
    # Removing the limit but setting the partition down, to double-check same/similar
    # results
    atf.run_command(
        f"scontrol update PartitionName=p1 {limit_name}=1 State=DOWN",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id = atf.submit_job_sbatch(f"-p p1 --wrap 'sleep infinity'")
    assert atf.wait_for_job_state(
        job_id, "PENDING", "PartitionDown"
    ), f"Verify that job is not run neither with partition down"
