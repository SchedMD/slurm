##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
"""Test that RequeueExit and RequeueExitHold are controlled by the origin.

As federation.shtml says: "slurm.conf options RequeueExit and RequeueExitHold
are controlled by the origin cluster". So only the origin (cls1) has them
configured here, and the jobs that run on its siblings exit with those codes.
"""

import pytest

import atf

from .conftest import (
    assert_restarts_count,
    check_dbd_states,
    check_exit_hold,
    cls1,
    submit_job_on_sibling,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow

# Read by the setup() of the conftest: the numbers of the clusters
requeue_exit_clusters = [1]


def test_requeue_exit_controlled_by_origin(
    federation_setup, cleanup, scripts, node_count
):
    """Test that RequeueExit of the origin requeues a job that ran in a sibling."""
    job_id, _ = submit_job_on_sibling(scripts["requeue_exit"], scripts, node_count)

    # The origin requeues it, as the siblings don't have RequeueExit
    check_dbd_states(job_id, cls1, ["REQUEUED"])
    rerun_cluster = wait_for_running_cluster(job_id)
    assert (
        rerun_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"
    assert_restarts_count(job_id, 1)


def test_requeue_exithold_controlled_by_origin(
    federation_setup, cleanup, scripts, node_count
):
    """Test that RequeueExitHold of the origin holds a job that ran in a sibling."""
    job_id, _ = submit_job_on_sibling(scripts["requeue_exithold"], scripts, node_count)

    # The origin holds it, as the siblings don't have RequeueExitHold
    atf.wait_for_job_state(
        job_id, "SPECIAL_EXIT", siblings=True, clusters=cls1, fatal=True
    )
    check_exit_hold(job_id)
    check_dbd_states(job_id, cls1, ["REQUEUED", "PENDING"])
