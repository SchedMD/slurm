##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    assert_restarts_count,
    check_dbd_states,
    cls1,
    cls2,
    cls3,
    wait_for_revoked_on_origin,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow


def test_requeue_running_on_sibling(federation_setup, cleanup, scripts, node_count):
    """Test requeuing a job running on a sibling cluster."""
    file_in = scripts["sleep_long"]

    # Fill cls1 explicitly so job routes to sibling
    fill_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
        f" --requeue -M{cls1} {file_in}",
        fatal=True,
    )
    atf.wait_for_job_state(fill_id, "RUNNING", siblings=True, clusters=cls1, fatal=True)

    # Submit job without constraint - will route to cls2 or cls3
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
        f" --requeue {file_in}",
        fatal=True,
    )

    c = wait_for_running_cluster(job_id)
    assert c in [cls2, cls3], f"Job should run on sibling cluster, got {c}"

    # Wait for origin to see the REVOKED state
    wait_for_revoked_on_origin(job_id)

    # Requeue from origin (cls1)
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # cls1's post-requeue PENDING is transient with requeue_delay=0; only
    # REVOKED gets flushed to the dbd.
    check_dbd_states(job_id, c, ["REQUEUED"])
    check_dbd_states(job_id, cls1, ["REVOKED"])

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient and it runs again in a free sibling.
    assert_restarts_count(job_id, 1)
    assert atf.wait_for_job_state(
        job_id, "RUNNING", siblings=True, clusters=[cls2, cls3]
    ), f"Requeued job {job_id} did not run again"
