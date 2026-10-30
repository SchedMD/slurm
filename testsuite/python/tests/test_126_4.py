############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test EnforcePartLimits=ALL rejects jobs exceeding a partition's CPUs."""

import pytest

import atf

FEATURE = "emptyfeat"
NODESET = "ns_emptyfeat"
PARTITION = "emptypart"
CPU_PARTITION = "cpupart"
MAXCPU_PARTITION = "maxcpupart"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Two CPUs per node let MAXCPU_PARTITION tell its MaxCPUsPerNode limit
    # apart from the CPUs the nodes have.
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter("EnforcePartLimits", "ALL")
    atf.require_config_parameter("Nodeset", {NODESET: {"Feature": FEATURE}})
    atf.require_config_parameter(
        "PartitionName",
        {
            PARTITION: {"Nodes": NODESET},
            CPU_PARTITION: {"Nodes": "ALL"},
            MAXCPU_PARTITION: {"Nodes": "ALL", "MaxCPUsPerNode": 1},
        },
    )
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "partition,size_from",
    [
        (PARTITION, None),
        (CPU_PARTITION, "TotalCPUs"),
        (MAXCPU_PARTITION, "TotalNodes"),
    ],
    ids=["empty", "total_cpus", "max_cpus_per_node"],
)
def test_reject_job_exceeding_partition_cpus(partition, size_from):
    """A job needing more CPUs than the partition allows is rejected.

    The empty partition has no CPUs at all. In CPU_PARTITION the job asks for
    one CPU more than the partition has. Every node has at least 2 CPUs and
    MAXCPU_PARTITION has MaxCPUsPerNode=1, so a job asking for one CPU more
    than the node count fits that partition's CPUs but exceeds its limit.
    """
    ntasks = 1
    if size_from:
        ntasks = atf.get_partitions()[partition][size_from] + 1

    result = atf.run_command(f"sbatch -p {partition} -n{ntasks} --wrap='true'")
    assert result["exit_code"] != 0, f"sbatch should be rejected in {partition}"
    assert (
        "More processors requested than permitted" in result["stderr"]
    ), f"sbatch should be rejected for its CPU count. Got: {result['stderr']}"


@pytest.mark.parametrize(
    "partition,size_from",
    [(CPU_PARTITION, "TotalCPUs"), (MAXCPU_PARTITION, "TotalNodes")],
    ids=["total_cpus", "max_cpus_per_node"],
)
def test_accept_job_at_partition_cpus(partition, size_from):
    """A job asking for exactly the CPUs the partition allows is accepted."""
    ntasks = atf.get_partitions()[partition][size_from]

    job_id = atf.submit_job_sbatch(f"-p {partition} -n{ntasks} --wrap='true'")
    assert job_id != 0, f"sbatch -n{ntasks} should be accepted in {partition}"
