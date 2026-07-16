############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test PriorityTier ordering of a pending multi-partition job.

Issue 50290: a pending multi-partition job lists its partitions in
PriorityTier order regardless of submission order, and a runtime
PriorityTier update reorders it.
"""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def partition_nodes():
    """Create two partitions sharing one node with different PriorityTier."""

    shared_node = list(atf.get_nodes().keys())[0]

    atf.run_command(
        f"scontrol create PartitionName=ptlow Nodes={shared_node} PriorityTier=1",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol create PartitionName=pthigh Nodes={shared_node} PriorityTier=100",
        fatal=True,
        user=atf.properties["slurm-user"],
    )

    yield shared_node

    atf.cancel_all_jobs()
    atf.run_command(
        "scontrol delete PartitionName=ptlow",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        "scontrol delete PartitionName=pthigh",
        user=atf.properties["slurm-user"],
    )


def _submit_pending_multi():
    """Submit a multi-partition job that pends, listing partitions in tier order.

    Fill the only CPU with a pthigh blocker, then submit to "ptlow,pthigh"
    (lower tier first); while pending its Partition string reads "pthigh,ptlow"
    (PriorityTier order, not submission order). A pending job's part_ptr is not
    surfaced, so this string and the eventual run partition are the only
    observable signals that part_ptr lands on the head. Returns (blocker_id,
    job_id).
    """

    blocker_id = atf.submit_job_sbatch(
        "-p pthigh -J blocker -t 5 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(blocker_id, "RUNNING"), "Blocker job should run"

    job_id = atf.submit_job_sbatch(
        "-p ptlow,pthigh -J multi -t 5 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Multi-partition job should be pending while the node is busy"

    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "pthigh,ptlow", (
        "Pending multi-partition job should list its partitions in PriorityTier "
        f"order regardless of submission order, got {partition}"
    )

    # squeue is a separate display path from scontrol and the documented
    # contract names it explicitly, so check its Partition column too.
    squeue_partition = atf.run_command_output(
        f"squeue -h -j {job_id} -o %P", fatal=True
    ).strip()
    assert squeue_partition == "pthigh,ptlow", (
        "squeue should list the pending job's partitions in PriorityTier order, "
        f"got {squeue_partition}"
    )
    return blocker_id, job_id


def test_multi_partition_job_runs_in_highest_priority_tier(partition_nodes):
    """A multi-partition job starts in the highest PriorityTier partition.

    Ticket 20893: the partition list is kept PriorityTier-sorted regardless of
    submission order. Submitted to "ptlow,pthigh" (lower tier first) against a
    busy CPU, the job reports "pthigh,ptlow" while pending and must run in
    pthigh once the CPU frees, not the first one submitted. Guarded here
    because Issue 50290 reworked the partition handling.
    """

    atf.require_version(
        (24, 11),
        reason="Ticket 20893: job runs in highest PriorityTier partition",
    )

    blocker_id, job_id = _submit_pending_multi()

    # Free the node; the job must start in the highest tier partition.
    atf.cancel_jobs([blocker_id], fatal=True)

    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Multi-partition job should run once the node frees"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "pthigh", (
        "Multi-partition job should run in the highest PriorityTier partition "
        f"regardless of submission order, got {partition}"
    )


def test_pending_partition_string_reorders_on_priority_tier_update(partition_nodes):
    """Raising a partition's PriorityTier reorders a pending job's partition list.

    Issue 50290: a pending job's Partition string is PriorityTier-ordered, and
    a runtime PriorityTier update must reorder it. The job is submitted to
    "ptlow,pthigh" (reported "pthigh,ptlow"); raising ptlow above pthigh must
    flip it to "ptlow,pthigh".
    """

    atf.require_version(
        (26, 11),
        reason="Issue 50290: partition string reorders on PriorityTier update",
    )

    blocker_id, job_id = _submit_pending_multi()

    # Raise ptlow above pthigh; the pending job's partition string must reorder.
    atf.run_command(
        "scontrol update PartitionName=ptlow PriorityTier=200",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    for _ in atf.timer():
        if atf.get_job_parameter(job_id, "Partition") == "ptlow,pthigh":
            break
    else:
        pytest.fail(
            "Raising ptlow's PriorityTier should reorder the pending job's "
            "partition string to ptlow,pthigh"
        )

    # squeue is a separate display path from scontrol, so the reorder must show
    # there too.
    squeue_partition = atf.run_command_output(
        f"squeue -h -j {job_id} -o %P", fatal=True
    ).strip()
    assert squeue_partition == "ptlow,pthigh", (
        "squeue should reflect the reordered partition string after the "
        f"PriorityTier update, got {squeue_partition}"
    )

    # Free the node; the job must run in ptlow (now the highest tier), proving
    # the runtime PriorityTier update repointed part_ptr to the new list head
    # rather than only reordering the displayed partition string.
    atf.cancel_jobs([blocker_id], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Multi-partition job should run once the node frees"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        "After raising ptlow's PriorityTier the job should run in ptlow (the now "
        f"highest tier partition), got {partition}"
    )
