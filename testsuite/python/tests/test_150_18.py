############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that DefSegmentSize changes reach pending jobs."""

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11), "sbin/slurmctld", reason="Issue 50961: DefSegmentSize added in 26.11"
    )
    atf.require_nodes(8)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    topology_config = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockSizes=4,8
    """
    atf.require_config_file("topology.conf", topology_config)
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def pending_jobs():
    """Fill the cluster, then leave two jobs pending behind it.

    The first does not request a segment size, the second does.
    """

    partition = atf.default_partition()
    prior_defaults = atf.get_partition_parameter(partition, "JobDefaults")
    set_job_defaults(partition, "DefSegmentSize=2")

    blocker = submit_job("-N 8")
    atf.wait_for_job_state(blocker, "RUNNING", fatal=True)

    default_job = submit_job("-N 4")
    requested_job = submit_job("-N 4 --segment=2")
    wait_for_segment_size(default_job, 2)
    wait_for_segment_size(requested_job, 2)

    yield default_job, requested_job

    set_job_defaults(partition, prior_defaults or "")


def submit_job(sbatch_args):
    return atf.submit_job_sbatch(
        f'{sbatch_args} --exclusive --mem=1 --wrap="sleep infinity"', fatal=True
    )


def set_job_defaults(partition, specs):
    atf.run_command(
        f"scontrol update PartitionName={partition} JobDefaults={specs}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def set_job_segment_size(job_id, segment_size):
    atf.run_command(
        f"scontrol update job={job_id} SegmentSize={segment_size}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def segment_size(job_id):
    return atf.get_job_parameter(
        job_id, "SegmentSize", default=0, quiet=True, fatal=True
    )


def wait_for_segment_size(job_id, expected):
    """Poll until the job's SegmentSize settles to the expected value.

    The default is applied by the scheduler on each evaluation rather than at
    submission, so after a DefSegmentSize change the new value only appears on
    the job's next scheduling pass.
    """
    for _ in atf.timer(fatal=True):
        if segment_size(job_id) == expected:
            return


def test_pending_job_follows_default_change(pending_jobs):
    """Verify a change of DefSegmentSize reaches pending jobs"""

    default_job, requested_job = pending_jobs

    set_job_defaults(atf.default_partition(), "DefSegmentSize=4")

    # A pending job which did not request a segment size tracks the new
    # default on its next evaluation; one which did is left alone.
    wait_for_segment_size(default_job, 4)
    assert (
        segment_size(requested_job) == 2
    ), "A job which requested a segment size should not be affected"

    new_job = submit_job("-N 4")
    wait_for_segment_size(new_job, 4)


def test_pending_job_follows_default_clear(pending_jobs):
    """Verify clearing DefSegmentSize also reaches pending jobs"""

    default_job, requested_job = pending_jobs

    set_job_defaults(atf.default_partition(), "")

    wait_for_segment_size(default_job, 0)
    assert (
        segment_size(requested_job) == 2
    ), "A job which requested a segment size should not be affected"

    new_job = submit_job("-N 4")
    wait_for_segment_size(new_job, 0)


def test_pending_job_can_opt_out(pending_jobs):
    """Verify a pending job can opt out of DefSegmentSize"""
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 50852: scontrol update job SegmentSize added in 26.11",
    )

    default_job, requested_job = pending_jobs

    # An explicit SegmentSize=0 pins the job as user-set, so the
    # partition default no longer applies to it: the job runs unsegmented.
    set_job_segment_size(default_job, 0)
    wait_for_segment_size(default_job, 0)

    # The default is only applied when the scheduler evaluates a job, so a
    # new job picking up the changed default shows that a scheduling pass
    # has run. The opted-out job must not have picked it up by then.
    set_job_defaults(atf.default_partition(), "DefSegmentSize=4")
    new_job = submit_job("-N 4")
    wait_for_segment_size(new_job, 4)
    assert (
        segment_size(default_job) == 0
    ), "Opted-out job should not get the changed default"
    assert (
        segment_size(requested_job) == 2
    ), "A job which requested a segment size should not be affected"


def test_invalid_value_rejected():
    """Verify a segment size which does not fit a uint16 is rejected"""

    output = atf.run_command_error(
        f"scontrol update PartitionName={atf.default_partition()} "
        "JobDefaults=DefSegmentSize=70000",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert "Invalid JobDefaults" in output, "Out of range value should be rejected"
