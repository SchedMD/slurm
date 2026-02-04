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


@pytest.mark.parametrize("external", [False, True], ids=["script", "external"])
def test_requeue_running_on_origin(
    federation_setup, cleanup, scripts, node_count, external
):
    """Test requeue of a running job on origin cluster, also an external one."""
    if external:
        atf.require_version(
            (26, 11),
            "sbin/slurmctld",
            reason="Issue 166 (slurm-bridge): external job requeue added in 26.11",
        )
        job_args = "--external"
    else:
        job_args = str(scripts["complete_script"])

    # Fill the siblings so the job runs on the origin cluster
    fill_cluster(cls2, scripts)
    fill_cluster(cls3, scripts)

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null --error=/dev/null"
        f" --requeue {job_args}",
        fatal=True,
    )
    run_cluster = wait_for_running_cluster(job_id, cls1)
    assert run_cluster == cls1, f"Job {job_id} not running on {cls1}"

    # Requeue the job while it is running
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient; the REQUEUED record in the dbd is not.
    check_dbd_states(job_id, run_cluster, ["REQUEUED"])
    assert (
        wait_for_running_cluster(job_id, cls1) == cls1
    ), f"Requeued job {job_id} did not run again on {cls1}"
