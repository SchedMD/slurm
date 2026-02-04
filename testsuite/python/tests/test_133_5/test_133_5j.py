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
    wait_for_origin_ack_cancel,
    wait_for_running_cluster,
    wait_for_sacct_state,
)

pytestmark = pytest.mark.slow


def test_requeue_cancelled_job_on_sibling(
    federation_setup, cleanup, scripts, node_count
):
    """Test requeuing a cancelled job that ran on a sibling cluster.

    Fill origin, submit job to sibling, wait for RUNNING. Cancel it,
    wait for DONE. Requeue. Wait for re-run
    on sibling (not origin). Verify SLURM_RESTART_COUNT=1.
    """
    file_in = scripts["complete_script"]
    output_file = atf.module_tmp_path / "requeue_cancelled_sibling_output"

    # Fill origin
    fill_cluster(cls1, scripts)

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output={output_file} --error=/dev/null"
        f" --requeue --open-mode=append {file_in}",
        fatal=True,
    )

    run_cluster = wait_for_running_cluster(job_id, f"{cls2},{cls3}")
    assert run_cluster in [cls2, cls3], f"Job should run on sibling, got {run_cluster}"

    # Cancel the running job
    atf.run_command(f"scancel {job_id}", fatal=True)

    # Wait for job to reach CANCELLED state in sacct
    wait_for_sacct_state(job_id, run_cluster, "CANCELLED", timeout=30)

    # sacct above only covers the running cluster; this waits for cls1 to
    # see the cancel, otherwise the requeue routes back to a dead sibling.
    assert wait_for_origin_ack_cancel(
        job_id, origin=cls1
    ), f"Origin {cls1} did not acknowledge the cancel of job {job_id}"

    # Requeue the cancelled job
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient and it can already run in a free sibling.
    # Only REVOKED gets flushed to dbd for cls1, and run_cluster's sacct holds
    # CANCELLED from the original instance.
    check_dbd_states(job_id, cls1, ["REVOKED"])
    check_dbd_states(job_id, run_cluster, ["CANCELLED"])

    # Wait for re-run on sibling (not origin)
    rerun_cluster = wait_for_running_cluster(job_id)
    assert (
        rerun_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"

    # Verify SLURM_RESTART_COUNT=1
    assert_restarts_count(job_id, 1)

    # Verify SLURM_RESTART_COUNT=1 in output file
    check_restart_count_in_output(output_file, 1)
