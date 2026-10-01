############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that pending reservation jobs block only their own partition."""

import pytest

import atf

RESV_NAME = "test_resv"
DRAIN_REASON = "'Test reservation jobs where they cannot be scheduled'"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2)
    # The main scheduler must be the only thing able to start a job here
    atf.require_config_parameter("SchedulerType", "sched/builtin")
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter(
        "PartitionName",
        {
            "resvpart": {"Nodes": "node1", "Default": "NO", "State": "UP"},
            "otherpart": {"Nodes": "node2", "Default": "YES", "State": "UP"},
        },
    )
    atf.require_slurm_running()


# Module scope so this outlives the jobs holding the reservation: a
# reservation still requested by an unfinished job cannot be deleted.
@pytest.fixture(scope="module")
def restore_resvpart():
    """Undo whichever of the reservation and the drain was made."""
    yield

    if RESV_NAME in atf.get_reservations(quiet=True, fatal=True):
        atf.run_command(
            f"scontrol delete reservation {RESV_NAME}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    # Resuming a node that needs no resume is an invalid state transition.
    if "DRAIN" in atf.get_node_parameter("node1", "state", default=[]):
        atf.run_command(
            "scontrol update nodename=node1 state=resume",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


@pytest.fixture(scope="module")
def drained_reserved_partition(restore_resvpart):
    """Reserve node1, then drain it, so nothing in 'resvpart' can be scheduled."""
    atf.run_command(
        f"scontrol create reservation reservationname={RESV_NAME} "
        f"user={atf.properties['test-user']} start=now duration=120 "
        "nodes=node1 partition=resvpart",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol update nodename=node1 state=drain reason={DRAIN_REASON}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    return RESV_NAME


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Ticket 25843: the main scheduler stopped walking the job queue when"
    " a second job failed to start in the same partition; fixed in 26.05.5",
)
def test_reservation_job_does_not_block_rest_of_queue(drained_reserved_partition):
    """A reservation job must not stop the scheduler walking the rest of the queue.

    sched_config.html scopes "no other jobs in that partition will be
    scheduled" to the partition a job was left pending in, so the idle node of
    'otherpart' must still be handed out.
    """
    job_opts = '-N1 -t5 --output=/dev/null --wrap="sleep infinity"'
    resv_cmd = f"-p resvpart --reservation={drained_reserved_partition} {job_opts}"
    first_resv_job_id = atf.submit_job_sbatch(resv_cmd, fatal=True)
    second_resv_job_id = atf.submit_job_sbatch(resv_cmd, fatal=True)
    for job_id in (first_resv_job_id, second_resv_job_id):
        assert atf.wait_for_job_state(
            job_id, "PENDING"
        ), f"Job {job_id} should be pending, 'resvpart' has no usable node"

    control_job_id = atf.submit_job_sbatch(f"-p otherpart {job_opts}", fatal=True)
    assert atf.wait_for_job_state(control_job_id, "RUNNING"), (
        f"Job {control_job_id} in the idle 'otherpart' partition must start even"
        f" though reservation job {second_resv_job_id} sorts ahead of it and is"
        " stuck pending in 'resvpart'"
    )
    for job_id in (first_resv_job_id, second_resv_job_id):
        assert atf.get_job_parameter(job_id, "JobState", fatal=True) == "PENDING", (
            f"Job {job_id} must still have been pending when job"
            f" {control_job_id} started, or the queue was never walked past it"
        )
