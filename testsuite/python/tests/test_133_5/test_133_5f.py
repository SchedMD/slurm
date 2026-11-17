##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    check_dbd_states,
    cls1,
    cls2,
    cls3,
    fill_cluster,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow


def test_requeue_subset_cluster_constraint(
    federation_setup, cleanup, scripts, node_count
):
    """Test requeue with cluster constraint excluding a cluster."""
    file_in = scripts["sleep_long"]

    # Fill the origin, so the job runs on the only other cluster allowed
    fill_cluster(cls1, scripts)

    # Submit job excluding cls3 (fc feature) using negative constraint
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
        f" --requeue --cluster-constraint='!fc' {file_in}",
        fatal=True,
    )

    c = wait_for_running_cluster(job_id)
    assert c == cls2, "Job should run on cls2 with constraint '!fc' and cls1 full"

    # Requeue
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # Verify sacct states after requeue
    check_dbd_states(job_id, c, ["REQUEUED"])

    # cls3 should not get any record for this job (constraint excludes fc). The
    # requeued job can start again at once, so look at it for some seconds.
    for _ in atf.timer(timeout=5, poll_interval=1):
        result = atf.run_command(
            f"scontrol -M{cls3} --local -a -o show job {job_id}",
        )
        combined = result.get("stdout", "") + result.get("stderr", "")
        assert (
            "Invalid job id" in combined
        ), f"Job should not exist on cls3 (excluded by constraint): {combined}"
