############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test partition DefSegmentSize with topology/block."""

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
    atf.require_config_parameter_includes("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")

    # Mark topology for teardown and overwrite with proper data.
    # require_config_parameter marks a file for teardown,
    #  but it doesn't allow us to write multiple lines easily to an external conf.
    # We're using it to create/mark the file.
    atf.require_config_parameter("", "", source="topology")
    # This is where we write the actual data
    overwrite_topology_conf()
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


def overwrite_topology_conf():
    conf = atf.properties["slurm-config-dir"] + "/topology.conf"
    content = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockSizes=4,8
    """
    atf.run_command(f"cat > {conf}", input=content, user="slurm", fatal=True)


@pytest.fixture(scope="function")
def part_with_default():
    """Configure DefSegmentSize=2 in slurm.conf for the default partition"""

    partition = atf.default_partition()
    atf.set_partition_parameter(partition, "DefSegmentSize", 2)
    yield partition
    atf.set_partition_parameter(partition, "DefSegmentSize", None)


def submit_job(sbatch_args):
    return atf.submit_job_sbatch(
        f'{sbatch_args} --exclusive --mem=1 --wrap="sleep infinity"', fatal=True
    )


def segment_size(job_id):
    return atf.get_job_parameter(
        job_id, "SegmentSize", default=0, quiet=True, fatal=True
    )


def test_no_default():
    """Verify jobs get no segment size when DefSegmentSize is not configured"""

    assert (
        atf.get_partition_parameter(atf.default_partition(), "JobDefaults") is None
    ), "No job defaults should be set"

    job_id = submit_job("-N 4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 0, "Job should have no segment size"


def test_default_applied(part_with_default):
    """Verify DefSegmentSize is used by a job which does not request one"""

    assert (
        atf.get_partition_parameter(part_with_default, "JobDefaults")
        == "DefSegmentSize=2"
    ), "DefSegmentSize should be reported as a job default"

    job_id = submit_job("-N 4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 2, "Job should use DefSegmentSize"


def test_job_request_wins(part_with_default):
    """Verify --segment takes precedence over DefSegmentSize"""

    job_id = submit_job("-N 4 --segment=4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 4, "Job should keep its requested segment size"


def test_default_always_applied(part_with_default):
    """Verify DefSegmentSize is applied even when it doesn't fit the job size"""

    # 3 is not a multiple of 2. The default is applied as-is, so this is
    # rejected just like an explicit --segment=2 would be. Without the default
    # the same job is accepted, see test_no_default_odd_node_count().
    assert (
        atf.submit_job_sbatch(
            '-N 3 --exclusive --mem=1 --wrap="sleep infinity"', xfail=True
        )
        == 0
    ), "Job should be rejected -- 3 % 2"


def test_no_default_odd_node_count():
    """Verify the same job is accepted when DefSegmentSize is not set"""

    job_id = submit_job("-N 3")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 0, "Job should have no segment size"
