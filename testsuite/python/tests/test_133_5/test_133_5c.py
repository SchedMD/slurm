##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import assert_restarts_count, wait_for_running_cluster

pytestmark = pytest.mark.slow


def test_requeue_completed_job_on_origin(
    federation_setup, cleanup, scripts, node_count
):
    """Test requeuing a completed job on origin cluster."""
    file_in = scripts["sleep_short"]

    # Submit without cluster-constraint (matches original) so job can
    # be requeued to all clusters after requeue
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
        f" --requeue {file_in}",
        fatal=True,
    )

    c = wait_for_running_cluster(job_id)

    # Wait for job to complete (use scontrol, not sacct)
    atf.wait_for_job_state(
        job_id, "DONE", siblings=True, clusters=c, timeout=180, fatal=True
    )

    # Requeue the completed job
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient and it can already run in a free cluster.
    wait_for_running_cluster(job_id)
    assert_restarts_count(job_id, 1)
