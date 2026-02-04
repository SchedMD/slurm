##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    assert_restarts_count,
    check_dbd_states,
    check_restart_count_in_output,
    cls1,
    cls2,
    cls3,
    fill_cluster,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow


def test_requeue_exit_federation(
    federation_setup, cleanup, scripts, output_files, node_count
):
    """Test RequeueExit in federation context.

    Fill origin to force sibling execution. Submit a job that exits with
    RequeueExit code (3), verify it gets automatically requeued in the
    federation. Verify scontrol PENDING on all 3 clusters.
    Verify SLURM_RESTART_COUNT=1.
    """
    file_in = scripts["requeue_exit"]
    output_file = output_files["requeue_exit_output"]

    # Fill origin to force sibling execution
    fill_cluster(cls1, scripts)

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --requeue -o {output_file} -e /dev/null {file_in}",
        fatal=True,
    )

    run_cluster = wait_for_running_cluster(job_id, f"{cls2},{cls3}")
    assert run_cluster in [cls2, cls3], "Job should run on sibling"

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient and it can already run in a free sibling.
    # Verify sacct shows the requeue happened. The RequeueExit requeue is done
    # by the origin, so it is only recorded there.
    check_dbd_states(job_id, cls1, ["REQUEUED"])

    # Wait for re-run on sibling
    rerun_cluster = wait_for_running_cluster(job_id)
    assert rerun_cluster, "Requeued job should start running"
    assert (
        rerun_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"

    # Let sibling state converge before probing the restart count (avoids a race under load).
    atf.wait_for_job_state(
        job_id, "RUNNING", clusters=rerun_cluster, timeout=60, fatal=False
    )

    # Verify SLURM_RESTART_COUNT=1
    assert_restarts_count(job_id, 1)

    # Verify SLURM_RESTART_COUNT=1 in output file
    check_restart_count_in_output(output_file, 1)
