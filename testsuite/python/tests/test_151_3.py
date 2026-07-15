############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter(
        "PartitionName",
        {
            "p1": {"Nodes": "ALL", "OverSubscribe": "YES", "Default": "YES"},
            "p2": {"Nodes": "ALL", "OverSubscribe": "YES"},
        },
    )

    # Allow more than 4 jobs in terms of memory
    atf.require_nodes(1, [("CPUs", 1), ("RealMemory", 2048)])
    atf.require_config_parameter("DefMemPerNode", 256)

    atf.require_slurm_running()


def test_oversubscribe_only():
    """
    Verify behavior of --oversubscribe option.
    Submits a job with --oversubscribe and --hold, then checks job parameters.
    """
    job_id = atf.submit_job_sbatch(
        "--oversubscribe --hold -t1 --wrap='sleep infinity'", fatal=True
    )

    # Issue 20588 (26.05): scontrol may show OverSubscribe=OK instead of YES.
    expected = {"YES", "OK"} if atf.get_version() >= (26, 5) else {"YES"}
    assert (
        atf.get_job_parameter(job_id, "OverSubscribe") in expected
    ), f"OverSubscribe flag should be one of {expected}"
    assert (
        atf.get_job_parameter(job_id, "Contiguous") == 0
    ), "Contiguous flag should be 0"


def test_contiguous_only():
    """
    Verify behavior of --contiguous option.
    Submits a job with --contiguous and --hold, then checks job parameters.
    """
    job_id = atf.submit_job_sbatch(
        "--contiguous --hold -t1 --wrap='sleep infinity'", fatal=True
    )

    # A non-shareable job shows OverSubscribe=NO, or OK on 26.05+ (Issue 20588).
    assert atf.get_job_parameter(job_id, "OverSubscribe") in {
        "NO",
        "OK",
    }, "OverSubscribe flag should be 'NO' or 'OK'"
    assert (
        atf.get_job_parameter(job_id, "Contiguous") == 1
    ), "Contiguous flag should be 1"


def test_overlapping_oversubscribe():
    """
    Jobs without -s on overlapping OverSubscribe=YES partitions shouldn't run concurrently.
    """
    # Submit first job to p1 without --oversubscribe
    job_id1 = atf.submit_job_sbatch("-p p1 --wrap='sleep infinity'", fatal=True)

    # Wait for job 1 to start running
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)

    # Submit second job to p2 without --oversubscribe
    job_id2 = atf.submit_job_sbatch("-p p2 --wrap='sleep infinity'", fatal=True)

    # Verify job 2 is PENDING with Reason=Resources
    assert atf.wait_for_job_state(
        job_id2,
        "PENDING",
        "Resources",
    ), "Job2 should be PENDING due Resources"

    # Verify job 1 is still running
    assert (
        atf.get_job_parameter(job_id1, "JobState") == "RUNNING"
    ), "Job 1 should still be RUNNING"


def test_overlapping_oversubscribe_sharing_restriction_1():
    """
    Job with -s should be blocked if a non -s job is running on overlapping partition.
    """
    # Submit first job to p2 without --oversubscribe
    job_id1 = atf.submit_job_sbatch("-p p2 --wrap='sleep infinity'", fatal=True)

    # Wait for job 1 to start running
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)

    # Submit second job to p1 *with* --oversubscribe
    job_id2 = atf.submit_job_sbatch(
        "-p p1 --oversubscribe --wrap='sleep infinity'", fatal=True
    )

    # Verify job 2 is PENDING with Reason=Resources
    assert atf.wait_for_job_state(
        job_id2,
        "PENDING",
        "Resources",
    ), "Job2 should be PENDING due Resources"

    # Verify job 1 is still running
    assert (
        atf.get_job_parameter(job_id1, "JobState") == "RUNNING"
    ), "Job 1 should still be RUNNING"


def test_overlapping_oversubscribe_sharing_restriction_2():
    """
    Job without -s should be blocked if a job with -s is running on overlapping partition.
    """
    # Submit first job to p2 *with* --oversubscribe
    job_id1 = atf.submit_job_sbatch(
        "-p p2 --oversubscribe --wrap='sleep infinity'", fatal=True
    )

    # Wait for job 1 to start running
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)

    # Submit second job to p1 *without* --oversubscribe
    job_id2 = atf.submit_job_sbatch("-p p1 --wrap='sleep infinity'", fatal=True)

    # Verify job 2 is PENDING with Reason=Resources
    assert atf.wait_for_job_state(
        job_id2,
        "PENDING",
        "Resources",
    ), "Job2 should be PENDING due Resources"

    # Verify job 1 is still running
    assert (
        atf.get_job_parameter(job_id1, "JobState") == "RUNNING"
    ), "Job 1 should still be RUNNING"


def test_oversubscribe_same_partition():
    """
    Test that two jobs *with* --oversubscribe can run on the same node
    if they are in the same partition (p1).
    """
    # Submit first job to p1 with --oversubscribe
    job_id1 = atf.submit_job_sbatch(
        "-p p1 --oversubscribe --wrap='sleep infinity'", fatal=True
    )

    # Wait for job 1 to start running
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)
    node1 = atf.get_job_parameter(job_id1, "NodeList")

    # Submit second job to p1 *with* --oversubscribe
    job_id2 = atf.submit_job_sbatch(
        "-p p1 --oversubscribe --wrap='sleep infinity'", fatal=True
    )

    # Wait for job 2 to start running (should run concurrently)
    atf.wait_for_job_state(job_id2, "RUNNING", fatal=True)
    node2 = atf.get_job_parameter(job_id2, "NodeList")

    # Sanity check
    if node1 is None or node2 is None:
        pytest.fail(f"Both nodes should be specified but got {node1} and {node2}")

    # Verify both jobs are running on the same node
    assert node1 == node2, f"Jobs should run on the same node ({node1} vs {node2})"
