##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

from .conftest import cls1, cls2, cls3, submit_jobs_with_steps

pytestmark = pytest.mark.slow


def assert_sorted_by_cluster(command, reverse=False):
    """Run squeue and verify that its lines are sorted by the cluster column,
    and that there are lines of all the clusters."""
    result = atf.run_command_output(command, fatal=True)
    clusters = [
        cluster
        for line in result.splitlines()
        for cluster in [cls1, cls2, cls3]
        if cluster in line
    ]
    assert set(clusters) == {cls1, cls2, cls3}, f"Missing clusters in:\n{result}"
    assert clusters == sorted(
        clusters, reverse=reverse
    ), f"Lines should be sorted by cluster ({'desc' if reverse else 'asc'}):\n{result}"


def test_squeue_operations(federation_setup, cleanup):
    """Test squeue --local, sorting, flag reorder, stepid error, and per-cluster steps."""
    ids, cluster_map = submit_jobs_with_steps()
    ji0, ji1, ji2 = ids

    # --local shows only local cluster's job
    local_job = None
    for jid, cluster in cluster_map.items():
        if cluster == cls1:
            local_job = jid
            break
    assert local_job is not None, "No job found running on cls1"
    result = atf.run_command_output("squeue --local --noheader", fatal=True)
    assert re.search(
        rf"\b{local_job}\b", result
    ), f"Local job {local_job} not found in --local output:\n{result}"

    # Ascending sort by cluster
    assert_sorted_by_cluster("squeue --noheader -s -O jobid,stepid,cluster -Scluster")

    # Descending sort by cluster
    assert_sorted_by_cluster(
        "squeue --noheader -s -O jobid,stepid,cluster -S-cluster", reverse=True
    )

    # stepid without -s produces error
    result = atf.run_command("squeue --noheader -O stepid")
    combined = result.get("stdout", "") + result.get("stderr", "")
    assert re.search(
        r"Invalid job format specification: stepid", combined
    ), f"Expected stepid error, got: {combined}"

    # Jobs sorted descending by cluster
    assert_sorted_by_cluster(
        "squeue --noheader -O jobid,cluster -S-cluster", reverse=True
    )

    # Flag reorder: -s -O ... same as -O ... -s
    result1 = atf.run_command_output(
        "squeue --noheader -s -O jobid,stepid,cluster", fatal=True
    )
    result2 = atf.run_command_output(
        "squeue --noheader -O jobid,stepid,cluster -s", fatal=True
    )
    assert (
        result1.strip() == result2.strip()
    ), f"Flag order should not matter:\n--- -s first ---\n{result1}\n--- -s last ---\n{result2}"

    # Per-cluster steps: verify correct cluster name per job
    for cluster in [cls1, cls2, cls3]:
        result = atf.run_command_output(
            f"squeue --noheader -s -O jobid,stepid,cluster -M{cluster}", fatal=True
        )
        jobs_on_cluster = [jid for jid, c in cluster_map.items() if c == cluster]
        for jid in jobs_on_cluster:
            assert re.search(
                rf"\b{jid}\b.*{cluster}", result
            ), f"Job {jid} steps not found on correct cluster {cluster}:\n{result}"
            job_lines = [ln for ln in result.strip().splitlines() if str(jid) in ln]
            for line in job_lines:
                assert (
                    cluster in line
                ), f"Job {jid} step line should show cluster {cluster}: {line}"
        jobs_not_on_cluster = [jid for jid, c in cluster_map.items() if c != cluster]
        for jid in jobs_not_on_cluster:
            lines = [ln for ln in result.strip().splitlines() if str(jid) in ln]
            for line in lines:
                assert (
                    cluster not in line or "REVOKED" in line.upper() or not line.strip()
                ), f"Job {jid} should not have active steps on {cluster}:\n{line}"
