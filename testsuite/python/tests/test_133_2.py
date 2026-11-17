##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

fed_name = "fed"
# The names of the clusters, set by setup()
cls1 = cls2 = cls3 = None
# Maximum local job ID (bits 0-25)
MAX_LOCAL_ID = 0x3FFFFFF


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(1)
    atf.require_slurm_running()
    yield


def get_clusterfed_info():
    """Get cluster federation information from database."""
    output = atf.run_command_output(
        f"sacctmgr show cluster federation={fed_name} "
        f'format="cluster%20,federation%20,id,controlhost,controlport,features%30,fedstate" -n -P',
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    clusters = {}
    for line in output.strip().splitlines():
        parts = line.split("|")
        if len(parts) >= 6:
            cluster_name = parts[0].strip()
            if cluster_name:
                clusters[cluster_name] = {
                    "id": int(parts[2]) if parts[2] else 0,
                    "host": parts[3].strip(),
                    "port": int(parts[4]) if parts[4] else 0,
                    "features": parts[5].strip() if len(parts) > 5 and parts[5] else "",
                    "state": (
                        parts[6].strip() if len(parts) > 6 and parts[6] else "ACTIVE"
                    ),
                }
    return clusters


def cleanup_federation():
    """Clean up federation."""
    atf.run_command(
        f"sacctmgr -i delete federation {fed_name}",
        user=atf.properties["slurm-user"],
    )


def submit_job_and_get_id(cluster_idx):
    """Submit a job to a specific cluster and return the job ID."""
    atf.select_cluster(cluster_idx)
    return atf.submit_job_sbatch('--wrap="hostname" -o/dev/null', fatal=True)


def verify_federation_job_id(cluster_idx, cluster_name, expected_cluster_id):
    """
    Submit a job to a federated cluster and verify the job ID structure.

    Federation job IDs have the cluster ID embedded in bits 26-31.
    """
    job_id = submit_job_and_get_id(cluster_idx)

    # Federation job IDs must be larger than MAX_LOCAL_ID
    assert (
        job_id > MAX_LOCAL_ID
    ), f"{cluster_name} didn't return a federation job ID ({job_id})"

    # Extract cluster ID from the job ID (bits 26-31)
    cluster_id = job_id >> 26

    # Verify cluster ID matches expected
    assert cluster_id == expected_cluster_id, (
        f"Job ID {job_id} from {cluster_name} has incorrect cluster ID "
        f"({cluster_id} != {expected_cluster_id})"
    )


def verify_local_job_id(cluster_idx, cluster_name):
    """
    Submit a job to a non-federated cluster and verify the job ID is local.

    Local job IDs must be less than or equal to MAX_LOCAL_ID.
    """
    job_id = submit_job_and_get_id(cluster_idx)

    # Non-federated job IDs must not exceed MAX_LOCAL_ID
    assert (
        job_id <= MAX_LOCAL_ID
    ), f"{cluster_name} didn't return a non-federation job ID ({job_id})"


@pytest.fixture(scope="module")
def federated_clusters():
    """
    Return cluster federation information.

    ATF's require_multicluster(federation=True) + require_slurm_running()
    already created the federation, so we just retrieve the cluster IDs from the
    database.
    """
    return get_clusterfed_info()


def test_federation_job_ids(federated_clusters):
    """
    Test federation job IDs are correctly structured with embedded cluster IDs.

    Federation job IDs have the structure:
    - Bits 0-25: local job ID
    - Bits 26-31: cluster ID

    Each cluster in the federation should return a job ID with its unique
    cluster ID embedded in bits 26-31.
    """
    verify_federation_job_id(1, cls1, federated_clusters[cls1]["id"])
    verify_federation_job_id(2, cls2, federated_clusters[cls2]["id"])
    verify_federation_job_id(3, cls3, federated_clusters[cls3]["id"])


def test_local_job_ids_after_federation_removal():
    """
    Test that jobs return to local IDs after clusters are removed from federation.

    After deleting the federation, clusters should no longer embed cluster IDs
    in job IDs - they should return to normal local job IDs (bits 0-25 only).
    """
    # Delete the federation (already set up by ATF)
    cleanup_federation()

    # Wait for the removal to reach the controllers, not just the slurmdbd
    atf.wait_for_federation_sync()

    # Verify each cluster returns local job IDs
    verify_local_job_id(1, cls1)
    verify_local_job_id(2, cls2)
    verify_local_job_id(3, cls3)
