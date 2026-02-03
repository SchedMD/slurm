##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

fed_name = "fed"
# The names of the clusters, set by setup()
cls1 = cls2 = cls3 = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(1)
    atf.require_slurm_running()


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


def check_fed_status(cluster_idx, clusters_info):
    """
    Test federation status via scontrol show federation from a specific cluster.

    This is the core verification function that validates:
    - Federation name appears in output
    - Each cluster appears with correct host, port, ID, state, features
    - Self cluster is marked as "Self:"
    - Sibling clusters show "Sibling:" with PersistConnSend/Recv status

    Uses polling to wait for persistent connections to stabilize.
    """
    atf.select_cluster(cluster_idx)

    # Build all expected patterns. If no clusters are expected, the output
    # should be empty (no federation).
    patterns = {}
    for cluster, info in clusters_info.items():
        expected_conn = info.get("conn", "")
        if expected_conn == "Self":
            patterns[cluster] = (
                rf"Self:\s+{re.escape(cluster)}:{re.escape(info['host'])}:{info['port']}\s+"
                rf"ID:{info['id']}\s+FedState:{info['state']}\s+Features:{re.escape(info['features'])}"
            )
        else:
            patterns[cluster] = (
                rf"Sibling:\s+{re.escape(cluster)}:{re.escape(info['host'])}:{info['port']}\s+"
                rf"ID:{info['id']}\s+FedState:{info['state']}\s+Features:{re.escape(info['features'])}\s+"
                rf"PersistConnSend/Recv:{re.escape(expected_conn)}"
            )

    # Federation header + Self/Sibling lines, to catch unexpected extra clusters
    expected_lines = 1 + len(clusters_info) if clusters_info else 0

    output = ""
    for _ in atf.timer(timeout=30):
        output = atf.run_command_output("scontrol show federation", fatal=True)
        if not clusters_info:
            found = output.strip() == ""
        else:
            lines = [line for line in output.splitlines() if line.strip()]
            found = (
                re.search(rf"Federation:\s+{fed_name}", output)
                and all(re.search(pattern, output) for pattern in patterns.values())
                and len(lines) == expected_lines
            )
        if found:
            break
    else:
        pytest.fail(
            f"Unexpected federation output from cluster {cluster_idx} "
            f"(expected {expected_lines} lines matching {list(patterns.values())}):\n"
            f"{output}"
        )


def cleanup_federation():
    """Clean up federation."""
    atf.run_command(
        f"sacctmgr -i delete federation {fed_name}",
        user=atf.properties["slurm-user"],
    )


def add_cluster_to_fed(cluster_name):
    """Add a cluster to federation."""
    output = atf.run_command_output(
        f"sacctmgr -i modify federation {fed_name} set clusters+={cluster_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "Setting" in output
    assert re.search(rf"Cluster\s+\+=\s+{cluster_name}", output)
    assert "Modified federation" in output


def remove_cluster_from_fed(cluster_name):
    """Remove a cluster from federation."""
    output = atf.run_command_output(
        f"sacctmgr -i modify federation {fed_name} set clusters-={cluster_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "Setting" in output
    assert re.search(rf"Cluster\s+-=\s+{cluster_name}", output)
    assert "Modified federation" in output


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level fixture to set up and tear down federation."""
    yield
    cleanup_federation()


def test_scontrol_show_federation(federation_setup):
    """
    Test scontrol show federation from each cluster perspective.

    Verifies that each cluster correctly shows:
    - Itself as "Self"
    - Other clusters as "Sibling" with persistent connection status (Yes/Yes)
    """
    # Get cluster info from database
    clusters_info = get_clusterfed_info()

    # Test from cls1 perspective
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    clusters_info[cls3]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)

    # Test from cls2 perspective
    clusters_info[cls1]["conn"] = "Yes/Yes"
    clusters_info[cls2]["conn"] = "Self"
    clusters_info[cls3]["conn"] = "Yes/Yes"
    check_fed_status(2, clusters_info)

    # Test from cls3 perspective
    clusters_info[cls1]["conn"] = "Yes/Yes"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    clusters_info[cls3]["conn"] = "Self"
    check_fed_status(3, clusters_info)


def test_remove_and_add_clusters(federation_setup):
    """
    Test removing and adding clusters, verifying scontrol show federation updates.

    The flow is:
    1. Remove cls3, verify from cls1 and cls2
    2. Remove cls1, verify from cls2
    3. Remove cls2, verify empty federation
    4. Add clusters back one by one, verifying after each
    """
    # Remove cls3 and test from cls1 and cls2
    remove_cluster_from_fed(cls3)

    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)

    clusters_info[cls1]["conn"] = "Yes/Yes"
    clusters_info[cls2]["conn"] = "Self"
    check_fed_status(2, clusters_info)

    # Remove cls1 and test from cls2
    remove_cluster_from_fed(cls1)

    clusters_info = get_clusterfed_info()
    clusters_info[cls2]["conn"] = "Self"
    check_fed_status(2, clusters_info)

    # Remove cls2 and test (should show no federation)
    remove_cluster_from_fed(cls2)
    check_fed_status(2, {})

    # Add cls1 back and test
    add_cluster_to_fed(cls1)
    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    check_fed_status(1, clusters_info)

    # Add cls2 and test from cls1
    add_cluster_to_fed(cls2)
    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)

    # Add cls3 and test from cls1
    add_cluster_to_fed(cls3)
    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    clusters_info[cls3]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)


def test_cluster_features_in_federation(federation_setup):
    """
    Test cluster features appear correctly in scontrol show federation.

    This tests that cluster features set via sacctmgr are correctly
    reflected in the scontrol show federation output. Each modification
    is verified by checking the federation status.
    """
    # Set features on cls3 (fb,fa - will be sorted alphabetically)
    output = atf.run_command_output(
        f"sacctmgr -i modify cluster {cls3} set features=fb,fa",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "Setting" in output
    assert re.search(r"Feature\s+=\s+fb", output)
    assert re.search(r"Feature\s+=\s+fa", output)
    assert "Modified cluster" in output
    assert re.search(rf"^\s+{cls3}", output, re.MULTILINE)

    # Verify features appear in scontrol show federation
    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    clusters_info[cls3]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)

    # Add feature fc
    output = atf.run_command_output(
        f"sacctmgr -i modify cluster {cls3} set features+=fc",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "Setting" in output
    assert re.search(r"Feature\s+\+=\s+fc", output)
    assert "Modified cluster" in output
    assert re.search(rf"^\s+{cls3}", output, re.MULTILINE)

    # Verify updated features in scontrol show federation
    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    clusters_info[cls3]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)

    # Remove feature fb
    output = atf.run_command_output(
        f"sacctmgr -i modify cluster {cls3} set features-=fb",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "Setting" in output
    assert re.search(r"Feature\s+-=\s+fb", output)
    assert "Modified cluster" in output
    assert re.search(rf"^\s+{cls3}", output, re.MULTILINE)

    # Verify updated features in scontrol show federation
    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    clusters_info[cls3]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)

    # Clear all features
    output = atf.run_command_output(
        f"sacctmgr -i modify cluster {cls3} set features=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "Setting" in output
    assert re.search(r"Feature\s+=\s*$", output, re.MULTILINE)
    assert "Modified cluster" in output
    assert re.search(rf"^\s+{cls3}", output, re.MULTILINE)

    # Verify features cleared in scontrol show federation
    clusters_info = get_clusterfed_info()
    clusters_info[cls1]["conn"] = "Self"
    clusters_info[cls2]["conn"] = "Yes/Yes"
    clusters_info[cls3]["conn"] = "Yes/Yes"
    check_fed_status(1, clusters_info)


def test_delete_federation(federation_setup):
    """
    Test deleting federation and verify scontrol show federation shows no federation.
    """
    # Delete federation
    cleanup_federation()

    # Verify no federation shown
    check_fed_status(2, {})
