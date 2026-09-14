############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a pending het job blocks its own partition and no other."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(3)
    # Het job submission requires sched/backfill, but only use the main scheduler
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", -1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter(
        "PartitionName",
        {
            "hetpart": {"Nodes": "node1,node2", "Default": "NO", "State": "UP"},
            "otherpart": {"Nodes": "node3", "Default": "YES", "State": "UP"},
        },
    )
    atf.require_slurm_running()


sched_break_reason = (
    "Ticket 25843: the main scheduler stopped walking the job queue when a"
    " second component failed to start in the same partition; fixed in 26.05.5"
)


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason=sched_break_reason,
)
def test_hetjob_does_not_block_lower_priority_job_in_other_partition():
    """A het job must not stop the scheduler from reaching the rest of the queue."""
    het_job_id = atf.submit_job_sbatch(
        '--output=/dev/null -p hetpart -N1 -t5 : -p hetpart -N1 -t5 --wrap="sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        het_job_id, "PENDING"
    ), f"Het job {het_job_id} should stay pending with the backfill loop disabled"

    control_job_id = atf.submit_job_sbatch(
        '-p otherpart -N1 -t5 --output=/dev/null --wrap="sleep infinity"', fatal=True
    )

    assert atf.wait_for_job_state(control_job_id, "RUNNING"), (
        f"Job {control_job_id} in the idle 'otherpart' partition must start even"
        f" though het job {het_job_id} sorts ahead of it and is stuck pending in"
        " 'hetpart'"
    )
    assert atf.get_job_parameter(het_job_id, "JobState", fatal=True) == "PENDING", (
        f"Het job {het_job_id} must still have been pending when job"
        f" {control_job_id} started, or the queue was never walked past it"
    )


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason=sched_break_reason,
)
def test_hetjob_blocks_lower_priority_job_in_same_partition():
    """A het job left pending must still block its own partition."""
    het_job_id = atf.submit_job_sbatch(
        '--output=/dev/null -p hetpart -N1 -t5 : -p hetpart -N1 -t5 --wrap="sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(
        het_job_id, "PENDING"
    ), f"Het job {het_job_id} should stay pending with the backfill loop disabled"

    blocked_job_id = atf.submit_job_sbatch(
        '-p hetpart -N1 -t5 --output=/dev/null --wrap="sleep infinity"', fatal=True
    )
    control_job_id = atf.submit_job_sbatch(
        '-p otherpart -N1 -t5 --output=/dev/null --wrap="sleep infinity"', fatal=True
    )

    assert atf.wait_for_job_state(control_job_id, "RUNNING"), (
        f"Job {control_job_id} in the idle 'otherpart' partition must start,"
        " which is what proves the scheduler walked the whole queue"
    )
    assert atf.get_job_parameter(het_job_id, "JobState", fatal=True) == "PENDING", (
        f"Het job {het_job_id} must still have been pending when job"
        f" {control_job_id} started, or 'hetpart' had no idle node left to block"
    )
    assert atf.get_job_parameter(blocked_job_id, "JobState", fatal=True) == "PENDING", (
        f"Job {blocked_job_id} must stay pending while het job {het_job_id} is"
        " left pending in 'hetpart', even though 'hetpart' has an idle node"
    )
