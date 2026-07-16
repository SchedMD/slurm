############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test a multi-partition job or array keeps its partition on restart.

Issue 50290: a job allocated a lower-tier partition (not the PriorityTier
sorted list head) must recover that same partition across an slurmctld
restart.
"""

import pytest

import atf

# Two overlapping partitions of different PriorityTier defined in slurm.conf so
# they survive the slurmctld restart these tests perform (runtime created
# partitions are not recovered on restart). pthigh (higher PriorityTier) spans
# node1 only; ptlow (lower tier) spans both nodes. With pthigh's only node busy
# a multi-partition job can only run in ptlow, so its allocated partition is the
# tail of the PriorityTier sorted partition list, not the head.
PARTITIONS = {
    "pthigh": {"Nodes": "node1", "PriorityTier": 100},
    "ptlow": {"Nodes": "node1,node2", "PriorityTier": 1},
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2, [("CPUs", 1)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("PartitionName", PARTITIONS)
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


def _classify_array_tasks(array_job_id):
    """Wait for the array to settle into one running + one pending task and
    return their (running_task_id, pending_task_id) get_jobs() keys.

    With one free node, one task runs and the other waits. Non-array jobs (the
    blocker) lack an ArrayJobId and are skipped. The returned get_jobs() keys
    are stable across an slurmctld restart, so the caller can re-query them.
    """

    tasks = {}

    def _settled():
        running_id = pending_id = 0
        for raw_id, job in atf.get_jobs(quiet=True).items():
            if job.get("ArrayJobId") != array_job_id:
                continue
            if job["JobState"] == "RUNNING":
                running_id = raw_id
            elif job["JobState"] == "PENDING":
                pending_id = raw_id
        if running_id and pending_id:
            tasks["ids"] = (running_id, pending_id)
            return True
        return False

    for _ in atf.timer(fatal=True):
        if _settled():
            break
    return tasks["ids"]


def test_multi_partition_running_job_partition_kept_on_restart():
    """A multi-partition running job keeps its allocated partition on restart.

    Ticket 22010: recovery assumed the first partition in the saved string was
    the allocated one, but part_ptr_list is PriorityTier-sorted, so a restart
    could reassign the job to the wrong (highest tier) partition (squeue then
    disagreeing with sacct). The job here is allocated the lower tier partition,
    so recovering the sorted head would be caught.
    """

    # Fill pthigh's only node so the multi-partition job cannot use pthigh.
    atf.require_version(
        (24, 11),
        reason="Ticket 22010: allocated partition recovered on restart",
    )

    blocker_id = atf.submit_job_sbatch(
        "-p pthigh -J blocker -t 10 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        blocker_id, "RUNNING"
    ), "Blocker job should run on the shared node"

    # The job prefers pthigh (higher tier) but it is full, so it lands in ptlow.
    job_id = atf.submit_job_sbatch(
        "-p ptlow,pthigh -J multi -t 10 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "Multi-partition job should run"

    partition_before = atf.get_job_parameter(job_id, "Partition")
    assert (
        partition_before == "ptlow"
    ), f"Job should be allocated the lower tier partition, got {partition_before}"

    # squeue is a separate display path from scontrol; a running job must show
    # only its single allocated partition there too.
    squeue_partition = atf.run_command_output(
        f"squeue -h -j {job_id} -o %P", fatal=True
    ).strip()
    assert (
        squeue_partition == "ptlow"
    ), f"squeue should show the single allocated partition, got {squeue_partition}"

    atf.restart_slurmctld()
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Multi-partition job should still be running after restart"

    # The 22010 regression corrupts the recovered part_ptr, which scontrol
    # reports. A regression would recover the sorted head (pthigh).
    partition_after = atf.get_job_parameter(job_id, "Partition")

    # The accounting partition column is written once at job start by
    # as_mysql_job_start() and not re-derived on restart, so sacct holds the
    # frozen start value (ptlow): the ground truth for where the job ran. Wait
    # for the asynchronous job-start record first (fatal), then check scontrol's
    # recovered partition matches it; recovering the sorted head (pthigh) would
    # fail it.
    atf.wait_for_job_accounted(job_id, field="Partition", value="ptlow", fatal=True)
    sacct_after = atf.run_command_output(
        f"sacct -XPnj {job_id} -o Partition", fatal=True
    ).strip()
    assert partition_after == sacct_after, (
        f"Recovered partition does not match accounting: scontrol reports "
        f"{partition_after}, sacct reports the frozen-at-start {sacct_after}"
    )


def test_multi_partition_array_keeps_partitions_on_restart():
    """A multi-partition job array keeps each task's partition across a restart.

    Ticket 22010: with node1 full, one task runs in ptlow and the other stays
    pending. Across a restart the running task must keep its allocated partition
    (ptlow), not the sorted head (pthigh), and the pending task must keep the
    full PriorityTier-sorted list (pthigh,ptlow). Guarded here because Issue
    50290 reworked recovery.
    """

    # Fill node1 (pthigh's only node) so the array can only reach node2 through
    # ptlow, forcing one task to run in the lower tier partition.
    atf.require_version(
        (24, 11),
        reason="Ticket 22010: recovery + PriorityTier-sorted list on restart",
    )

    blocker_id = atf.submit_job_sbatch(
        "-p pthigh -w node1 -J blocker -t 10 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        blocker_id, "RUNNING"
    ), "Blocker job should run on node1"

    array_id = atf.submit_job_sbatch(
        "-p ptlow,pthigh --array=0-1 -J multiarr -t 10 --wrap='sleep infinity'",
        fatal=True,
    )
    # With a single free node one task runs and the other stays pending.
    running_id, pending_id = _classify_array_tasks(array_id)

    # The running task is allocated the lower tier partition; the pending task
    # surfaces the full PriorityTier sorted list with no single partition
    # prepended.
    assert (
        atf.get_job_parameter(running_id, "Partition") == "ptlow"
    ), "Running array task should be allocated the lower tier partition (ptlow)"
    assert (
        atf.get_job_parameter(pending_id, "Partition") == "pthigh,ptlow"
    ), "Pending array task should list its partitions in PriorityTier order"

    atf.restart_slurmctld()

    # The get_jobs() keys are stable across the restart; re-query them.
    assert atf.wait_for_job_state(
        running_id, "RUNNING"
    ), "Running array task should still be running after restart"
    assert (
        atf.get_job_parameter(running_id, "Partition") == "ptlow"
    ), "Running array task's allocated partition should still be ptlow after restart"

    assert atf.wait_for_job_state(
        pending_id, "PENDING"
    ), "Pending array task should still be pending after restart"
    assert (
        atf.get_job_parameter(pending_id, "Partition") == "pthigh,ptlow"
    ), "Pending array task should still list both partitions in PriorityTier order"
