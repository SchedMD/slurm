############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test a multi-partition job keeps its partition across runtime changes.

Issue 50290: a job allocated the lowest-tier partition must keep or
correctly rebuild that partition across delete, reconfigure, suspend,
requeue, PriorityTier updates and restart, never jumping to the sorted
list head.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

# Canonical partition layout: three overlapping partitions of distinct
# PriorityTier. pthigh spans node1 only, ptmid spans node1,node2 and ptlow
# spans all three nodes. With node1 and node2 busy a job submitted to all
# three can only run in ptlow (the lowest tier), so its allocated partition is
# the tail of the PriorityTier sorted partition list, not the head.
PARTITIONS = {
    "pthigh": {"Nodes": "node1", "PriorityTier": 100},
    "ptmid": {"Nodes": "node1,node2", "PriorityTier": 50},
    "ptlow": {"Nodes": "node1,node2,node3", "PriorityTier": 1},
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(3, [("CPUs", 1)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # Define the partitions in slurm.conf rather than with 'scontrol create' so
    # they survive the slurmctld restart some of these tests perform (runtime
    # created partitions are not recovered on restart).
    atf.require_config_parameter("PartitionName", PARTITIONS)
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def restore_partitions():
    yield
    # Tests delete a partition at runtime or drop one from slurm.conf; rewrite
    # the canonical layout (this reconfigures Slurm) so the next test starts
    # clean with all three partitions present.
    atf.set_config_parameter("PartitionName", PARTITIONS)


def _submit_job_running_in_ptlow(extra_args=""):
    """Submit blockers filling node1,node2 plus a job that lands in ptlow.

    Returns (blocker_ids, job_id). The job lands in ptlow (lowest tier) since
    the higher tiers are full. Blockers use ptlow (spans every node), so none
    holds ptmid -- it stays a deletable secondary while node3 is left free for
    the job. extra_args is appended to the job submission.
    """

    # Fill node1 (pthigh's only node) and node2 so only node3 stays free. Both
    # blockers run in ptlow and are pinned with -w, so neither holds ptmid (the
    # secondary the delete test removes) and node3 is reserved for the multi
    # job.
    blocker_ids = []
    for node in ("node1", "node2"):
        blocker_id = atf.submit_job_sbatch(
            f"-p ptlow -w {node} -J blocker -t 10 --wrap='sleep infinity'",
            fatal=True,
        )
        assert atf.wait_for_job_state(
            blocker_id, "RUNNING"
        ), f"Blocker job on {node} should run"
        blocker_ids.append(blocker_id)

    # The job prefers the higher tiers but only node3 (ptlow) is free.
    args = "-p ptlow,ptmid,pthigh -J multi -t 10 --wrap='sleep infinity'"
    if extra_args:
        args = f"{extra_args} {args}"
    job_id = atf.submit_job_sbatch(args, fatal=True)
    assert atf.wait_for_job_state(job_id, "RUNNING"), "Multi-partition job should run"

    partition = atf.get_job_parameter(job_id, "Partition")
    assert (
        partition == "ptlow"
    ), f"Job should be allocated the lowest tier partition, got {partition}"

    return blocker_ids, job_id


def _assert_partition_matches_accounting(job_id, scontrol_partition, context):
    # The accounting partition column is written once at job start by
    # as_mysql_job_start() and not re-derived by the delete/reconfigure/
    # PriorityTier-update paths here, so sacct holds the frozen start value
    # (ptlow): the ground truth for where the job ran. Wait for the
    # asynchronous job-start record first (fatal), then check that the
    # in-memory partition matches that frozen value.
    atf.wait_for_job_accounted(job_id, field="Partition", value="ptlow", fatal=True)
    sacct_partition = atf.run_command_output(
        f"sacct -XPnj {job_id} -o Partition", fatal=True
    ).strip()
    assert scontrol_partition == sacct_partition, (
        f"{context}: kept partition does not match accounting: scontrol "
        f"reports {scontrol_partition}, sacct reports the frozen-at-start "
        f"{sacct_partition}"
    )


def test_suspended_multi_partition_partition_kept_on_restart():
    """A suspended multi-partition job keeps its partition across a restart.

    Recovery treats suspended jobs like running ones, so a suspended job
    allocated ptlow must keep it across a restart, not be reassigned to the
    sorted head (pthigh), and resume correctly.
    """

    atf.require_version(
        (24, 11),
        reason="Ticket 22010: allocated partition recovered on restart",
    )

    _, job_id = _submit_job_running_in_ptlow()

    atf.run_command(
        f"scontrol suspend {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(job_id, "SUSPENDED"), "Job should be suspended"

    atf.restart_slurmctld()
    assert atf.wait_for_job_state(
        job_id, "SUSPENDED"
    ), "Job should still be suspended after restart"

    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Suspended job's allocated partition changed across restart to "
        f"{partition}, expected ptlow"
    )

    atf.run_command(
        f"scontrol resume {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job should resume to running"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert (
        partition == "ptlow"
    ), f"Resumed job's allocated partition changed to {partition}, expected ptlow"


def test_priority_tier_update_keeps_running_job_partition_on_restart():
    """A runtime PriorityTier update keeps a running job's partition on restart.

    Ticket 22010: a runtime 'scontrol update ... PriorityTier=...' re-sorts
    every job's part_ptr_list and rebuilds its string. A running job's part_ptr
    must stay on its allocated partition (ptlow), not move to the new sorted
    head -- here raising ptmid above pthigh makes ptmid the head. The allocated
    partition must survive both the update and a restart. Guarded here because
    Issue 50290 reworked recovery.
    """

    atf.require_version(
        (24, 11),
        reason="Ticket 22010: allocated partition kept across PriorityTier update",
    )

    _, job_id = _submit_job_running_in_ptlow()

    # Raise ptmid above pthigh so the PriorityTier sorted head changes from
    # pthigh to ptmid. ptmid spans only the busy node1,node2, so this frees
    # nothing and the job keeps running in ptlow.
    atf.run_command(
        "scontrol update PartitionName=ptmid PriorityTier=200",
        fatal=True,
        user=atf.properties["slurm-user"],
    )

    # The running job must keep its allocated partition (ptlow); a regression
    # that repointed part_ptr to the new sorted head would report ptmid.
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Job should stay running across the PriorityTier update"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Updating a partition's PriorityTier moved the running job to "
        f"{partition}, expected ptlow"
    )

    # A restart must recover the same allocated partition rather than the new
    # sorted head.
    atf.restart_slurmctld()
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Job should still be running after restart"
    partition = atf.get_job_parameter(job_id, "Partition")
    _assert_partition_matches_accounting(
        job_id,
        partition,
        "Running job's allocated partition changed across restart",
    )


def test_delete_secondary_partition_keeps_running_job_partition():
    """Deleting a secondary partition must not move a running job's partition.

    Ticket 22010: a running job allocated ptlow has the sorted list
    [pthigh, ptmid, ptlow]. Deleting the middle secondary (ptmid) must leave the
    allocated partition (ptlow) unchanged, not repoint part_ptr to the head.
    Three partitions are needed: with two, the surviving head equals the
    allocated partition and the regression would not show.
    """

    atf.require_version(
        (26, 11),
        reason="Issue 50290: allocated partition kept on partition delete",
    )

    _, job_id = _submit_job_running_in_ptlow()

    # Deleting the job's allocated partition is refused while it is in use.
    result = atf.run_command(
        "scontrol delete PartitionName=ptlow",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "Deleting the allocated partition of a running job should fail"
    assert (
        "in use" in result["stderr"].lower()
    ), f"Expected a partition-in-use error, got: {result['stderr']}"

    # Deleting the middle secondary partition is allowed and must not move the
    # running job off its allocated partition.
    atf.run_command(
        "scontrol delete PartitionName=ptmid",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Job should stay running after deleting a secondary partition"

    # The regression repoints the recovered part_ptr to the list head, which
    # scontrol reports, so the load-bearing check is that scontrol still reports
    # ptlow after the delete. A regression would flip it to pthigh.
    partition = atf.get_job_parameter(job_id, "Partition")
    _assert_partition_matches_accounting(
        job_id,
        partition,
        "Deleting the middle secondary partition moved the running job",
    )


def test_reconfigure_keeps_running_job_partition():
    """A plain reconfigure keeps a running job's allocated partition.

    The reconfigure recovery path (_sync_jobs_to_conf) must keep a running
    multi-partition job on its allocated ptlow when slurm.conf is unchanged,
    not re-derive the PriorityTier sorted head.
    """

    atf.require_version(
        (24, 11),
        reason="Ticket 22010: allocated partition recovered on reconfigure",
    )

    _, job_id = _submit_job_running_in_ptlow()

    atf.run_command(
        "scontrol reconfigure",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Job should stay running across a plain reconfigure"
    partition = atf.get_job_parameter(job_id, "Partition")
    _assert_partition_matches_accounting(
        job_id,
        partition,
        "Running job's allocated partition changed across a plain reconfigure",
    )


def test_reconfigure_keeps_suspended_job_partition():
    """A plain reconfigure keeps a suspended job's allocated partition.

    Recovery treats suspended jobs like running ones, so the reconfigure
    recovery path (_sync_jobs_to_conf) must keep a suspended multi-partition
    job on its allocated ptlow when slurm.conf is unchanged, not re-derive the
    PriorityTier sorted head, and it must resume correctly.
    """

    atf.require_version(
        (24, 11),
        reason="Ticket 22010: suspended job's partition recovered on reconfigure",
    )

    _, job_id = _submit_job_running_in_ptlow()

    atf.run_command(
        f"scontrol suspend {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(job_id, "SUSPENDED"), "Job should be suspended"

    atf.run_command(
        "scontrol reconfigure",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "SUSPENDED"
    ), "Job should stay suspended across a plain reconfigure"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Suspended job's allocated partition changed across a plain "
        f"reconfigure to {partition}, expected ptlow"
    )

    atf.run_command(
        f"scontrol resume {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job should resume to running"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert (
        partition == "ptlow"
    ), f"Resumed job's allocated partition changed to {partition}, expected ptlow"


def test_reconfigure_dropping_secondary_keeps_running_job_partition():
    """Reconfigure dropping a secondary rebuilds a running job's partition list.

    Ticket 22010: dropping a surviving secondary from slurm.conf makes
    _sync_jobs_to_conf() see an invalid token and rebuild the string -- a path
    a plain reconfigure never reaches. The allocated ptlow must be kept and the
    rebuilt sorted list must drop ptmid. A running job shows only its single
    partition, so the rebuilt list is checked after requeuing it to pending.
    """

    atf.require_version(
        (26, 11),
        reason="Issue 50290: partition list rebuilt on reconfigure",
    )

    _, job_id = _submit_job_running_in_ptlow("--requeue")

    # Drop the surviving secondary ptmid from slurm.conf and reconfigure. The
    # allocated ptlow survives, so the job keeps running on it; ptmid becomes an
    # invalid token in the partition string and the list is rebuilt without it.
    surviving = {k: v for k, v in PARTITIONS.items() if k != "ptmid"}
    atf.set_config_parameter("PartitionName", surviving)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Job should stay running when only a secondary partition is dropped"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Dropping a secondary partition moved the running job to {partition}, "
        "expected ptlow"
    )

    # Requeue back to pending to expose the rebuilt partition list. It must list
    # the surviving partitions in PriorityTier order with the dropped secondary
    # (ptmid) gone; a regression that did not rebuild would still list ptmid.
    atf.run_command(
        f"scontrol requeuehold {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Requeued job should return to pending"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "pthigh,ptlow", (
        f"Rebuilt partition list should drop the removed secondary and stay in "
        f"PriorityTier order, got {partition}"
    )


def test_delete_secondary_partition_keeps_suspended_job_partition():
    """Deleting a secondary partition must not move a suspended job's partition.

    The suspended counterpart of the running-job delete path: a suspended job
    allocated ptlow must keep it when a secondary (ptmid) is deleted, not be
    repointed to the head (pthigh) or killed, and resume correctly.
    """

    atf.require_version(
        (26, 11),
        reason="Issue 50290: allocated partition kept on partition delete",
    )

    _, job_id = _submit_job_running_in_ptlow()

    atf.run_command(
        f"scontrol suspend {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(job_id, "SUSPENDED"), "Job should be suspended"

    # ptmid holds no job, so deleting it is allowed even while the job is
    # suspended; the allocated partition (ptlow) must be left untouched.
    atf.run_command(
        "scontrol delete PartitionName=ptmid",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "SUSPENDED"
    ), "Job should stay suspended after deleting a secondary partition"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Deleting a secondary partition moved the suspended job to "
        f"{partition}, expected ptlow"
    )

    atf.run_command(
        f"scontrol resume {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "Job should resume to running"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert (
        partition == "ptlow"
    ), f"Resumed job's allocated partition changed to {partition}, expected ptlow"


def test_requeued_multi_partition_restores_partition_list():
    """Requeuing a running multi-partition job restores its full partition set.

    A running job is allocated a single partition (ptlow); once requeued back to
    pending it must surface its full PriorityTier sorted partition list again,
    with no single allocated partition prepended. That list survives an slurmctld
    restart unchanged.
    """

    atf.require_version(
        (26, 11),
        reason="Issue 50290: requeued job's partition list in PriorityTier order",
    )

    _, job_id = _submit_job_running_in_ptlow("--requeue")

    # Requeue and hold so the job stays pending deterministically while node3
    # is still free.
    atf.run_command(
        f"scontrol requeuehold {job_id}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Requeued job should return to pending"

    # A pending multi-partition job surfaces its PriorityTier sorted list, not
    # the single partition it had been allocated.
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "pthigh,ptmid,ptlow", (
        f"Requeued pending job should list all partitions in PriorityTier "
        f"order, got {partition}"
    )

    atf.restart_slurmctld()
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Requeued job should still be pending after restart"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "pthigh,ptmid,ptlow", (
        f"Requeued pending job changed its partition list across restart to "
        f"{partition}, expected pthigh,ptmid,ptlow"
    )


def test_delete_secondary_partition_keeps_pending_job_partition():
    """Deleting a secondary partition keeps a pending job's primary partition.

    A pending job's part_ptr is the sorted list head (pthigh), now set by
    rebuild_job_part_list() rather than an explicit peek in the delete handler.
    Deleting the middle secondary (ptmid) goes through
    _foreach_kill_job_by_part_name(), which rebuilds the list and must keep
    part_ptr on the surviving head and the string in PriorityTier order
    (pthigh,ptmid,ptlow -> pthigh,ptlow). Deleting the primary (pthigh) is
    refused, since a pending job's part_ptr marks it in use.
    """

    # Fill every node so the multi-partition job cannot start and stays pending.
    # The blockers run in ptlow (which spans all nodes) so none of them holds
    # ptmid or pthigh as its part_ptr, leaving both deletable as far as job use
    # is concerned.
    atf.require_version(
        (26, 11),
        reason="Issue 50290: pending job's partition list in PriorityTier order",
    )

    for node in ("node1", "node2", "node3"):
        blocker_id = atf.submit_job_sbatch(
            f"-p ptlow -w {node} -J blocker -t 10 --wrap='sleep infinity'",
            fatal=True,
        )
        assert atf.wait_for_job_state(
            blocker_id, "RUNNING"
        ), f"Blocker job on {node} should run"

    job_id = atf.submit_job_sbatch(
        "-p ptlow,ptmid,pthigh -J multi -t 10 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Multi-partition job should be pending while every node is busy"

    # The pending job lists all partitions in PriorityTier order; the head
    # (pthigh) is its primary partition.
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "pthigh,ptmid,ptlow", (
        f"Pending multi-partition job should list its partitions in PriorityTier "
        f"order, got {partition}"
    )

    # Deleting the primary partition (the pending job's part_ptr) is refused
    # because a pending job marks it in use.
    result = atf.run_command(
        "scontrol delete PartitionName=pthigh",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "Deleting the primary partition of a pending job should fail"
    assert (
        "in use" in result["stderr"].lower()
    ), f"Expected a partition-in-use error, got: {result['stderr']}"

    # Deleting the middle secondary (ptmid) is allowed and must rebuild the
    # partition list, dropping ptmid and keeping PriorityTier order, with the
    # primary partition (the head, pthigh) unchanged.
    atf.run_command(
        "scontrol delete PartitionName=ptmid",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), "Job should stay pending after deleting a secondary partition"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "pthigh,ptlow", (
        f"Deleting the middle secondary should drop ptmid and keep PriorityTier "
        f"order, got {partition}"
    )
