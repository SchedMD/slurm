############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test DefSegmentSize ignored without segment support."""

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11), "sbin/slurmctld", reason="Issue 50961: DefSegmentSize added in 26.11"
    )
    atf.require_nodes(4)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # topology/flat has no notion of segments
    atf.require_config_parameter("TopologyPlugin", "topology/flat")
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def part_with_default():
    """Configure DefSegmentSize=2 in slurm.conf for the default partition"""

    partition = atf.default_partition()
    atf.set_partition_parameter(partition, "DefSegmentSize", 2)
    yield partition
    atf.set_partition_parameter(partition, "DefSegmentSize", None)


def _logfile():
    return atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)


def test_ignored_without_topology_support(part_with_default):
    """Verify DefSegmentSize is ignored by a topology without segments"""

    assert (
        atf.get_partition_parameter(part_with_default, "JobDefaults")
        == "DefSegmentSize=2"
    ), "DefSegmentSize should still be reported as a job default"

    job_id = atf.submit_job_sbatch(
        '-N 4 --exclusive --mem=1 --wrap="sleep infinity"', fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "SegmentSize", default=0, fatal=True) == 0
    ), "DefSegmentSize should not be applied without a segment-aware topology"


def test_warning_logged_for_unsupported_topology(part_with_default):
    """Verify slurmctld warns that the partition DefSegmentSize is a no-op"""

    # part_with_default set the default and reconfigured, so the warning the
    # docs promise should appear in the slurmctld log.
    pattern = (
        f"DefSegmentSize is set for partition {part_with_default}, "
        "but its topology does not support segments"
    )
    for _ in atf.timer(fatal=True):
        count = atf.run_command_output(
            f'grep -c -F -- "{pattern}" {_logfile()} || true',
            user=atf.properties["slurm-user"],
            quiet=True,
        )
        if int((count or "0").strip() or 0) > 0:
            break
