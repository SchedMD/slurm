##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

from .conftest import (
    cls1,
    cls2,
    cls3,
    run_squeue_test,
    squeue_steps,
    submit_fed_job,
    wait_for_steps_started,
)

pytestmark = pytest.mark.slow


def test_federated_squeue_output(federation_setup, cleanup):
    """Test federated squeue output."""
    ji0 = submit_fed_job(cls1)
    ji1 = submit_fed_job(cls2)
    ji2 = submit_fed_job(cls3)

    atf.wait_for_job_state(ji0, "RUNNING", siblings=True, clusters=cls1, fatal=True)
    atf.wait_for_job_state(ji1, "RUNNING", siblings=True, clusters=cls2, fatal=True)
    atf.wait_for_job_state(ji2, "RUNNING", siblings=True, clusters=cls3, fatal=True)

    # Wait for job steps to be visible
    cluster_map = {ji0: cls1, ji1: cls2, ji2: cls3}
    wait_for_steps_started(cluster_map)
    steps = squeue_steps()

    # Test basic squeue with steps, whose default format truncates the step ids
    # to 15 characters
    run_squeue_test(
        "-s",
        ".+".join(
            re.escape(f"{jid}.{step}"[:15]) for jid in [ji0, ji1, ji2] for step in steps
        ),
    )

    # Test squeue with output format and sorting - verify per-job steps with correct cluster
    result = atf.run_command_output(
        "squeue --noheader -s -O jobid,stepid,cluster", fatal=True
    )
    for jid in [ji0, ji1, ji2]:
        expected_cluster = cluster_map[jid]
        for step in steps:
            assert re.search(
                rf"\b{jid}\b\s+{jid}\.{step}\s+{expected_cluster}", result
            ), (
                f"Job {jid} step .{step} not found with cluster {expected_cluster} "
                f"in -O jobid,stepid,cluster output:\n{result}"
            )

    # The local view has the job of the local cluster
    run_squeue_test("--local", f"{ji0}.+R")

    # Test cluster-specific squeue - each cluster has its own running job
    for jid, cluster in cluster_map.items():
        run_squeue_test(f"-M{cluster}", f"{jid}.+R")

    # Test the sorting by cluster, ascending and descending
    def get_clusters(options):
        output = atf.run_command_output(f"squeue --noheader {options}", fatal=True)
        return [line.split()[-1] for line in output.splitlines() if line.strip()]

    step_count = 3 * len(steps)
    clusters = get_clusters("-s -O jobid,stepid,cluster -Scluster")
    assert (
        len(clusters) == step_count
    ), f"Expected {step_count} steps sorted by cluster: {clusters}"
    assert clusters == sorted(clusters), f"Steps not sorted by cluster: {clusters}"

    clusters = get_clusters("-s -O jobid,stepid,cluster -S-cluster")
    assert (
        len(clusters) == step_count
    ), f"Expected {step_count} steps sorted by -cluster: {clusters}"
    assert clusters == sorted(
        clusters, reverse=True
    ), f"Steps not sorted by -cluster: {clusters}"

    clusters = get_clusters("-O jobid,cluster -S-cluster")
    assert clusters == [cls3, cls2, cls1], f"Jobs not sorted by -cluster: {clusters}"

    # The step id can only be printed with the steps option
    result = atf.run_command("squeue -O jobid,stepid,cluster -Scluster")
    assert (
        "Invalid job format specification: stepid" in result["stderr"]
    ), f"Expected an error about stepid without -s: {result['stderr']}"
