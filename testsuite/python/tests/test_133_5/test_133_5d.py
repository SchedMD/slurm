##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import assert_restarts_count, cls1, cls2, cls3, fill_cluster

pytestmark = pytest.mark.slow


def test_requeue_failed_job_on_origin(federation_setup, cleanup, scripts):
    """Test requeuing a failed job on origin cluster."""
    file_in = scripts["fail_script"]

    # Fill the siblings so the job runs on the origin cluster
    fill_cluster(cls2, scripts)
    fill_cluster(cls3, scripts)

    # Submit with -N1 (matches original) without cluster-constraint
    job_id = atf.submit_job_sbatch(
        f"-N1 --output=/dev/null --error=/dev/null --requeue {file_in}",
        fatal=True,
    )

    # Wait for job to fail (use scontrol, not sacct)
    assert atf.wait_for_job_state(
        job_id, "DONE", siblings=True, clusters=cls1, timeout=180
    ), f"Job {job_id} did not fail on {cls1}"

    # Requeue the failed job
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient; the restart count is not.
    assert_restarts_count(job_id, 1)
