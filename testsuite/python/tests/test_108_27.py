############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol release/update of pending jobs in a partition with no nodes."""

import pytest

import atf

FEATURE = "emptyfeat"
NODESET = "ns_emptyfeat"
PARTITION = "emptypart"
CPU_PARTITION = "cpupart"
MAXCPU_PARTITION = "maxcpupart"

# A job in a partition with no nodes requests more CPUs than the partition
# has (zero), so it reports PartitionCpuLimit and stays pending. The same
# reason also covers a populated partition that simply doesn't have enough
# CPUs for the job.
REASON = "PartitionCpuLimit"


def expected_reason():
    """Reason name rendered for these jobs by this combination of versions.

    Ticket 25769: PartitionCpuLimit replaces PartitionConfig in 26.11. A
    slurmctld that sends the new reason to an older scontrol is rendered as
    InvalidReason, so there is nothing to compare against in that case.
    """
    if atf.get_version("sbin/slurmctld") < (26, 11):
        return "PartitionConfig"
    if atf.get_version("bin/scontrol") < (26, 11):
        pytest.skip("scontrol older than 26.11 can't render PartitionCpuLimit")
    return REASON


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Two CPUs per node let MAXCPU_PARTITION tell its MaxCPUsPerNode limit
    # apart from the CPUs the nodes have.
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter("EnforcePartLimits", "NO")
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

    # No node has FEATURE, so the NodeSet resolves to nothing.
    nodes = atf.run_command_output(f"sinfo -h -p {PARTITION} -o %N", fatal=True).strip()
    if nodes:
        pytest.fail(
            f"Precondition: partition {PARTITION} should have no nodes, got '{nodes}'"
        )


def test_submit_to_empty_partition_succeeds():
    """EnforcePartLimits=NO permits submission to an empty partition.

    Guards the documented behavior that the update-path fix must not regress.
    """
    reason = expected_reason()

    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION} --wrap='sleep infinity'", fatal=True
    )

    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason=reason
    ), f"Job {job_id} should be PENDING with Reason={reason}"


@pytest.mark.skipif(
    min(atf.get_version("sbin/slurmctld"), atf.get_version("bin/scontrol")) < (26, 11),
    reason="Ticket 25769: scontrol update returned an error for jobs "
    "exceeding a partition's CPU limit with EnforcePartLimits=NO before "
    "26.11.",
)
@pytest.mark.parametrize(
    "partition,size_from",
    [
        (PARTITION, None),
        (CPU_PARTITION, "TotalCPUs"),
        (MAXCPU_PARTITION, "TotalNodes"),
    ],
    ids=["empty", "total_cpus", "max_cpus_per_node"],
)
def test_update_job_exceeding_partition_cpu_limit(partition, size_from):
    """scontrol update succeeds for a job pending on PartitionCpuLimit.

    An empty partition is only the degenerate case (0 total CPUs). A job
    that asks for one CPU more than a populated partition has pends the same
    way. Every node has at least 2 CPUs and MAXCPU_PARTITION has
    MaxCPUsPerNode=1, so a job asking for one CPU more than the node count
    fits that partition's CPUs but exceeds its limit.
    """
    ntasks = 1
    if size_from:
        ntasks = atf.get_partitions()[partition][size_from] + 1
    job_id = atf.submit_job_sbatch(
        f"-p {partition} -n{ntasks} --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason=REASON
    ), f"Job {job_id} should be PENDING with Reason={REASON}"

    result = atf.run_command(f"scontrol update jobid={job_id} nice=100")
    assert result["exit_code"] == 0, (
        f"scontrol update should succeed for a job exceeding its "
        f"partition's CPU limit. Got stderr: {result['stderr']}"
    )
    assert result["stderr"] == "", (
        f"scontrol update should not report an error. "
        f"Got stderr: {result['stderr']}"
    )
    assert (
        atf.get_job_parameter(job_id, "Nice") == 100
    ), f"Job {job_id}'s Nice value should have been updated to 100"


@pytest.mark.skipif(
    min(atf.get_version("sbin/slurmctld"), atf.get_version("bin/scontrol")) < (26, 11),
    reason="Ticket 25769: scontrol release returned exit code 1 for a held job "
    "in an empty partition with EnforcePartLimits=NO before 26.11.",
)
def test_release_held_job_in_empty_partition():
    """scontrol release succeeds for a held job in an empty partition.

    The hold was always lifted, but the RPC returned
    ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE, so scontrol exited 1.
    """
    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION} -H --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="JobHeldUser"
    ), f"Job {job_id} should start out held"

    result = atf.run_command(f"scontrol release {job_id}")
    assert result["exit_code"] == 0, (
        f"scontrol release should succeed for a held job in an empty "
        f"partition. Got stderr: {result['stderr']}"
    )
    assert result["stderr"] == "", (
        f"scontrol release should not report an error. "
        f"Got stderr: {result['stderr']}"
    )

    # The hold is gone and the job now waits on the partition instead.
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason=REASON
    ), f"Released job {job_id} should be PENDING with Reason={REASON}"


@pytest.fixture
def restore_features():
    saved = {
        n: (
            atf.get_node_parameter(n, "features", default=[]),
            atf.get_node_parameter(n, "active_features", default=[]),
        )
        for n in atf.nodes
    }
    yield
    for node, (avail, active) in saved.items():
        avail_str = ",".join(avail) if avail else ""
        active_str = ",".join(active) if active else ""
        atf.run_command(
            f"scontrol update nodename={node} "
            f"activefeatures={active_str} availablefeatures={avail_str}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 50815: nodes did not join feature-based NodeSet partitions "
    "after scontrol update before 26.11.",
)
def test_job_starts_once_partition_gains_a_node(restore_features, request):
    """A PartitionCpuLimit job un-pends once its partition gains a node.

    Guards the scheduler actually retrying the job as the cluster changes,
    not just suppressing the misleading error.
    """
    reason = expected_reason()
    node = sorted(atf.nodes)[0]

    # select/linear keeps its own per-node partition list, which a feature
    # update does not refresh, so the job picks the node but can't start.
    if atf.get_config_parameter("SelectType", quiet=True) == "select/linear":
        request.applymarker(
            pytest.mark.xfail(
                reason="Nodes joining a NodeSet partition after a feature "
                "update are unknown to select/linear"
            )
        )

    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION} --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason=reason
    ), f"Job {job_id} should be PENDING with Reason={reason}"

    atf.run_command(
        f"scontrol update nodename={node} availablefeatures={FEATURE}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} should start once {node} joins {PARTITION}"
