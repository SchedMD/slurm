############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom topology.conf")
    atf.require_nodes(8)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    atf.require_version((25, 11), "sbin/slurmctld")

    # Mark topology for teardown and overwrite with proper data.
    # require_config_parameter marks a file for teardown,
    #  but it doesn't allow us to write multiple lines easily to an external conf.
    # We're using it to create/mark the file.
    atf.require_config_parameter("", "", source="topology")
    # This is where we write the actual data
    overwrite_topology_conf()
    atf.require_config_parameter_includes("SchedulerParameters", "bf_interval=1")
    atf.require_config_parameter_includes("SchedulerParameters", "sched_interval=1")
    atf.require_slurm_running()


def overwrite_topology_conf():
    conf = atf.properties["slurm-config-dir"] + "/topology.conf"
    content = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockSizes=4,8
    """
    atf.run_command(f"cat > {conf}", input=content, user="slurm", fatal=True)


def test_segment_nodelist():
    """Test simplest segment and nodelist"""

    job_id_1 = atf.submit_job_sbatch(
        '-N 4 -w node[1-4] --segment=2 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_1, "COMPLETED", fatal=True, timeout=5)

    job_id_2 = atf.submit_job_sbatch(
        '-N 4 -w node[1,2,5,6] --segment=2 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_2, "COMPLETED", fatal=True, timeout=5)

    job_id_3 = atf.submit_job_sbatch(
        '-N 6 -w node[1-8] --segment=3 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_3, "COMPLETED", fatal=True, timeout=5)

    job_id_4 = atf.submit_job_sbatch(
        '-N 6 -w node[1-3,5-7] --segment=3 --exclusive --mem=1 --wrap="exit 0"'
    )
    atf.wait_for_job_state(job_id_4, "COMPLETED", fatal=True, timeout=5)

    assert (
        atf.submit_job_sbatch(
            '-N 6 -w node[1-6] --segment=3 --exclusive --mem=1 --wrap="sleep 20"',
            xfail=True,
        )
        == 0
    ), "Job should fail segment #2 not fit on b2 "

    assert (
        atf.submit_job_sbatch(
            '-N 6 -w node[1-5] --segment=3 --exclusive --mem=1 --wrap="sleep 20"',
            xfail=True,
        )
        == 0
    ), "Job should fail requested node count > number of specified nodes"


def test_segment_update():
    """Test scontrol update job SegmentSize on a pending job."""

    atf.require_version((26, 11), "bin/scontrol")

    job_id = atf.submit_job_sbatch(
        '--hold -N 4 --segment=2 --exclusive --mem=1 --wrap="true"',
        fatal=True,
    )
    assert atf.get_job_parameter(job_id, "SegmentSize") == 2

    # SegmentSize=4 -> success (4 % 4 == 0).
    atf.run_command(f"scontrol update JobId={job_id} SegmentSize=4", fatal=True)
    assert atf.get_job_parameter(job_id, "SegmentSize") == 4

    # SegmentSize > min_nodes is allowed.
    atf.run_command(f"scontrol update JobId={job_id} SegmentSize=8", fatal=True)
    assert atf.get_job_parameter(job_id, "SegmentSize") == 8

    # SegmentSize=3 -> rejected (4 % 3 != 0), value unchanged.
    result = atf.run_command(
        f"scontrol update JobId={job_id} SegmentSize=3", xfail=True
    )
    assert result["exit_code"] != 0, "SegmentSize=3 must be rejected for N=4"
    assert atf.get_job_parameter(job_id, "SegmentSize") == 8

    # SegmentSize=0 clears segmentation.
    atf.run_command(f"scontrol update JobId={job_id} SegmentSize=0", fatal=True)
    assert atf.get_job_parameter(job_id, "SegmentSize") is None

    atf.cancel_jobs([job_id], quiet=True)


def test_segment_update_not_pending():
    """SegmentSize cannot be updated on a non-pending job."""

    atf.require_version((26, 11), "bin/scontrol")

    job_id = atf.submit_job_sbatch(
        "-N 2 --exclusive --mem=1 --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    result = atf.run_command(
        f"scontrol update JobId={job_id} SegmentSize=2", xfail=True
    )
    assert result["exit_code"] != 0, "SegmentSize update must fail for non-pending job"

    atf.cancel_jobs([job_id], quiet=True)


def test_spread_consolidate_segments_update():
    """Test scontrol update job SpreadSegments and ConsolidateSegments."""

    atf.require_version((26, 11), "bin/scontrol")

    job_id = atf.submit_job_sbatch(
        '--hold -N 4 --segment=2 --exclusive --mem=1 --wrap="true"',
        fatal=True,
    )
    assert atf.get_job_parameter(job_id, "SpreadSegments") is None
    assert atf.get_job_parameter(job_id, "ConsolidateSegments") is None

    # Enable both flags.
    atf.run_command(f"scontrol update JobId={job_id} SpreadSegments=Yes", fatal=True)
    assert atf.get_job_parameter(job_id, "SpreadSegments") == "Yes"

    atf.run_command(
        f"scontrol update JobId={job_id} ConsolidateSegments=Yes", fatal=True
    )
    assert atf.get_job_parameter(job_id, "ConsolidateSegments") == "Yes"

    # Setting an already-set flag is a silent no-op.
    atf.run_command(f"scontrol update JobId={job_id} SpreadSegments=Yes", fatal=True)
    assert atf.get_job_parameter(job_id, "SpreadSegments") == "Yes"

    # Clear flags.
    atf.run_command(f"scontrol update JobId={job_id} SpreadSegments=No", fatal=True)
    assert atf.get_job_parameter(job_id, "SpreadSegments") is None

    atf.run_command(
        f"scontrol update JobId={job_id} ConsolidateSegments=No", fatal=True
    )
    assert atf.get_job_parameter(job_id, "ConsolidateSegments") is None

    atf.cancel_jobs([job_id], quiet=True)


def test_spread_consolidate_segments_update_not_pending():
    """SpreadSegments/ConsolidateSegments cannot be updated on a non-pending job."""

    atf.require_version((26, 11), "bin/scontrol")

    job_id = atf.submit_job_sbatch(
        "-N 2 --exclusive --mem=1 --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    result = atf.run_command(
        f"scontrol update JobId={job_id} SpreadSegments=Yes", xfail=True
    )
    assert (
        result["exit_code"] != 0
    ), "SpreadSegments update must fail for non-pending job"

    result = atf.run_command(
        f"scontrol update JobId={job_id} ConsolidateSegments=Yes", xfail=True
    )
    assert (
        result["exit_code"] != 0
    ), "ConsolidateSegments update must fail for non-pending job"

    atf.cancel_jobs([job_id], quiet=True)
