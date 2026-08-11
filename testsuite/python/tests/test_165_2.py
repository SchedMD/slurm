############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test a completing multi-partition job keeps its partition on recovery.

Issue 50290: a completing job (epilog still running) is non-pending, so
restart and reconfigure must recover its allocated partition, not the
PriorityTier sorted list head.
"""

from pathlib import Path

import pytest

import atf

# A multi-partition job that is completing (its epilog still running) is
# non-pending, so state recovery must recover its allocated partition the same
# way it does for a running or suspended job, rather than the PriorityTier
# sorted list head.
EPILOG_TIMEOUT = 300

# Marker file the Epilog blocks on, created before each test and removed in the
# teardown so the COMPLETING window lasts exactly as long as the test body
# rather than a fixed timeout. Resolved to an absolute path under the test's cwd
# in setup(), since the isolated working directory is only in place once the
# module starts.
epilog_marker = None

# Three overlapping partitions of distinct PriorityTier. pthigh spans node1 only,
# ptmid spans node1,node2 and ptlow spans all three nodes. With node1 and node2
# busy a job submitted to all three can only run in ptlow (the lowest tier), so
# its allocated partition is the tail of the PriorityTier sorted list, not the
# head.
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
    # they survive the slurmctld restart these tests perform (runtime created
    # partitions are not recovered on restart).
    atf.require_config_parameter("PartitionName", PARTITIONS)

    # An Epilog that blocks while the marker file exists holds a finishing job
    # in COMPLETING; the teardown removes the marker so it exits without hitting
    # PrologEpilogTimeout (generous, so it fires only on a failed test).
    global epilog_marker
    epilog_marker = str(Path.cwd() / "epilog_block")
    epilog = str(Path.cwd() / "epilog.sh")
    atf.make_bash_script(
        epilog,
        f"while [ -f {epilog_marker} ]; do sleep 0.5; done\nexit 0\n",
    )
    atf.require_config_parameter("Epilog", epilog)
    atf.require_config_parameter("PrologEpilogTimeout", EPILOG_TIMEOUT)

    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def hold_completing(setup):
    # Create the marker so a cancelled job's epilog blocks and the job stays
    # COMPLETING for the test body.
    Path(epilog_marker).touch()
    yield
    # Remove the marker so every cancelled job's epilog exits and the job leaves
    # COMPLETING promptly, then wait the jobs out. Without this the epilog would
    # block until PrologEpilogTimeout and slurmctld would drain the node.
    Path(epilog_marker).unlink(missing_ok=True)
    atf.cancel_all_jobs(fatal=True)
    # Tests delete a partition at runtime or drop one from slurm.conf; rewrite
    # the canonical layout (this reconfigures Slurm) so the next test starts
    # clean with all three partitions present.
    atf.set_config_parameter("PartitionName", PARTITIONS)
    # If an epilog timed out earlier its node was drained (a reconfigure does
    # not clear that); resume all three so the next test's blockers can start.
    # Safety net: removing the marker above should keep nodes from draining.
    for node in ("node1", "node2", "node3"):
        atf.run_command(
            f"scontrol update nodename={node} state=RESUME",
            user=atf.properties["slurm-user"],
            quiet=True,
        )
        atf.wait_for_node_state(node, "IDLE", fatal=True)


def _submit_completing_job_in_ptlow():
    """Fill node1,node2 then submit a multi-partition job, cancel it completing.

    Returns the completing job's id. The job lands in ptlow (the lowest tier,
    so the tail of the sorted list) since the higher tiers are full; cancelling
    it leaves it COMPLETING while the marker epilog blocks.
    """

    # Fill node1 (pthigh's only node) and node2 so only node3 stays free. Both
    # blockers run in ptlow and are pinned with -w, so neither holds ptmid (the
    # secondary the delete tests remove) and node3 is reserved for the multi
    # job (reached through ptlow, the lowest tier partition).
    for node in ("node1", "node2"):
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
    assert atf.wait_for_job_state(job_id, "RUNNING"), "Multi-partition job should run"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert (
        partition == "ptlow"
    ), f"Job should be allocated the lowest tier partition, got {partition}"

    # Signal the job and observe it enter COMPLETING; the marker epilog holds it
    # there. A bare scancel is used rather than atf.cancel_jobs() because the
    # latter waits for a terminal state, which a job intentionally held in
    # COMPLETING never reaches before timing out.
    atf.run_command(f"scancel {job_id}", user=atf.properties["slurm-user"], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "COMPLETING"
    ), "Job should be completing while its epilog runs"

    return job_id


def test_completing_multi_partition_partition_kept_on_restart():
    """A completing multi-partition job keeps its allocated partition on restart.

    A completing job (terminal base state, epilog still running) is non-pending,
    so recovery must restore its allocated partition (ptlow), not the sorted
    head (pthigh). A marker epilog holds it COMPLETING across both recovery
    paths -- a restart and a reconfigure. Guarded here because Issue 50290
    reworked recovery.
    """

    atf.require_version(
        (24, 11),
        reason="Ticket 22010: allocated partition recovered on restart",
    )

    job_id = _submit_completing_job_in_ptlow()

    atf.restart_slurmctld()
    assert atf.wait_for_job_state(
        job_id, "COMPLETING"
    ), "Job should still be completing after restart"

    # The completing job must report the partition it ran in (ptlow). A
    # regression that recovered the sorted head would report pthigh.
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Completing job's allocated partition changed across restart to "
        f"{partition}, expected ptlow"
    )

    # A reconfigure re-derives part_ptr through the other recovery path while
    # the job is still completing; the allocated partition must survive it too.
    atf.run_command(
        "scontrol reconfigure",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETING"
    ), "Job should still be completing after reconfigure"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Completing job's allocated partition changed across reconfigure to "
        f"{partition}, expected ptlow"
    )


def test_completing_keeps_partition_when_secondary_removed_on_reconfigure():
    """Reconfigure dropping a secondary keeps a completing job's partition.

    The reconfigure path (_sync_jobs_to_conf) must also keep a completing job on
    its allocated partition (ptlow) when a surviving secondary (ptmid) is
    dropped from slurm.conf, not re-derive the sorted head.
    """

    atf.require_version(
        (25, 11),
        reason="Ticket 22929: partition kept when secondary removed on reconfigure",
    )

    job_id = _submit_completing_job_in_ptlow()

    surviving = {k: v for k, v in PARTITIONS.items() if k != "ptmid"}
    atf.set_config_parameter("PartitionName", surviving)
    assert atf.wait_for_job_state(
        job_id, "COMPLETING"
    ), "Job should stay completing after dropping a secondary partition"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Dropping a secondary partition moved the completing job to "
        f"{partition}, expected ptlow"
    )


def test_completing_keeps_partition_when_secondary_deleted():
    """Deleting a secondary partition must not move a completing job's partition.

    A completing job is IS_JOB_FINISHED, so partition_in_use() does not block
    deleting its secondaries. Deleting the middle one (ptmid) must leave the
    allocated partition (ptlow) unchanged; the old recovery repointed part_ptr
    to the list head (pthigh).
    """

    atf.require_version(
        (26, 11),
        reason="Issue 50290: allocated partition kept on partition delete",
    )

    job_id = _submit_completing_job_in_ptlow()

    # ptmid holds no job, so deleting it is allowed; the completing job's
    # allocated partition (ptlow) must be left untouched and the job not killed.
    atf.run_command(
        "scontrol delete PartitionName=ptmid",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETING"
    ), "Job should stay completing after deleting a secondary partition"
    partition = atf.get_job_parameter(job_id, "Partition")
    assert partition == "ptlow", (
        f"Deleting a secondary partition moved the completing job to "
        f"{partition}, expected ptlow"
    )
