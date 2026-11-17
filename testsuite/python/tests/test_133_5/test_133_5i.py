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


def test_requeue_completed_job_on_sibling(
    federation_setup, cleanup, scripts, node_count
):
    """Test requeuing a completed job that ran on a sibling cluster.

    Fill origin cluster, submit job that routes to sibling, wait for COMPLETED.
    Requeue. Check sacct states.
    Wait for re-run, verify SLURM_RESTART_COUNT=1, verify REVOKED on third sibling.
    """
    file_in = scripts["complete_script"]
    output_file = atf.module_tmp_path / "requeue_completed_sibling_output"

    # Fill origin so job routes to sibling
    fill_cluster(cls1, scripts)

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output={output_file} --error=/dev/null"
        f" --requeue --open-mode=append {file_in}",
        fatal=True,
    )

    run_cluster = wait_for_running_cluster(job_id, f"{cls2},{cls3}")
    assert run_cluster in [cls2, cls3], f"Job should run on sibling, got {run_cluster}"

    # Wait for job to complete on sibling (use scontrol, not sacct, to minimize
    # delay before requeue with MinJobAge)
    atf.wait_for_job_state(
        job_id, "DONE", siblings=True, clusters=run_cluster, timeout=180, fatal=True
    )

    # Requeue the completed job immediately
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # Check sacct states
    check_dbd_states(job_id, cls1, ["REVOKED"])
    check_dbd_states(job_id, run_cluster, ["COMPLETED"])

    # Verify REVOKED on the third sibling (not origin, not run_cluster)
    third_cluster = [c for c in [cls1, cls2, cls3] if c != cls1 and c != run_cluster][0]
    check_dbd_states(job_id, third_cluster, ["REVOKED"])

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient and it can already run in a free cluster.
    # Wait for re-run on any cluster
    rerun_cluster = wait_for_running_cluster(job_id)
    assert rerun_cluster, "Requeued job should start running on some cluster"
    assert (
        rerun_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"

    # Verify SLURM_RESTART_COUNT=1 via scontrol
    assert_restarts_count(job_id, 1)

    # Verify SLURM_RESTART_COUNT=1 in output file
    check_restart_count_in_output(output_file, 1)
