##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
"""Test that RequeueExit and RequeueExitHold of the siblings are ignored.

As federation.shtml says: "slurm.conf options RequeueExit and RequeueExitHold
are controlled by the origin cluster". So only the siblings (cls2 and cls3)
have them configured here, and the jobs that run on them exit with those codes
without being requeued nor held. It is the opposite control of test_133_5q.
"""

import pytest

import atf

from .conftest import assert_restarts_count, submit_job_on_sibling

pytestmark = pytest.mark.slow

# Read by the setup() of the conftest: the numbers of the clusters
requeue_exit_clusters = [2, 3]


def test_requeue_exit_ignored_in_siblings(
    federation_setup, cleanup, scripts, node_count
):
    """Test that RequeueExit of the siblings does not requeue the job."""
    job_id, run_cluster = submit_job_on_sibling(
        scripts["requeue_exit"], scripts, node_count
    )

    # It exits with 3, but the origin does not have RequeueExit
    atf.wait_for_job_state(
        job_id, "FAILED", siblings=True, clusters=run_cluster, fatal=True
    )
    assert_restarts_count(job_id, 0)


def test_requeue_exithold_ignored_in_siblings(
    federation_setup, cleanup, scripts, node_count
):
    """Test that RequeueExitHold of the siblings does not hold the job."""
    job_id, run_cluster = submit_job_on_sibling(
        scripts["requeue_exithold"], scripts, node_count
    )

    # It exits with 4, but the origin does not have RequeueExitHold
    atf.wait_for_job_state(
        job_id, "FAILED", siblings=True, clusters=run_cluster, fatal=True
    )
    assert_restarts_count(job_id, 0)
