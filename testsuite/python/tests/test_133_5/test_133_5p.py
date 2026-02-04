##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    assert_restarts_count,
    check_ctl_state,
    check_dbd_states,
    check_exit_hold,
    check_restart_count_in_output,
    cls1,
    cls2,
    cls3,
    fill_cluster,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow


def test_requeue_exithold_federation(
    federation_setup, cleanup, scripts, output_files, node_count
):
    """Test RequeueExitHold in federation context.

    Fill origin, submit RequeueExitHold job to sibling. Verify SPECIAL_EXIT.
    check_exit_hold (Priority=0 + Reason=JobHeldUser). Release.
    Verify PENDING on all 3. Verify SLURM_RESTART_COUNT=1.
    """
    file_in = scripts["requeue_exithold"]
    output_file = output_files["requeue_exithold_output"]

    # Fill origin to force sibling execution
    fill_cluster(cls1, scripts)

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --requeue -o {output_file} -e /dev/null {file_in}",
        fatal=True,
    )

    run_cluster = wait_for_running_cluster(job_id, f"{cls2},{cls3}")
    assert run_cluster in [cls2, cls3], "Job should run on sibling"

    # Wait for SPECIAL_EXIT on origin
    atf.wait_for_job_state(
        job_id, "SPECIAL_EXIT", siblings=True, clusters=cls1, timeout=180, fatal=True
    )

    check_exit_hold(job_id)
    check_ctl_state(job_id, cls1, "SPECIAL_EXIT")
    check_dbd_states(job_id, cls1, ["REQUEUED", "PENDING"])

    # Release hold
    atf.run_command(f"scontrol release {job_id}", fatal=True)

    # With requeue_delay=0 the released job is eligible right away, so its
    # PENDING state is transient and it can already run in a free sibling.
    # Wait for re-run on sibling
    rerun_cluster = wait_for_running_cluster(job_id)
    assert rerun_cluster, "Requeued job should start running"
    assert (
        rerun_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"

    # Verify SLURM_RESTART_COUNT=1
    assert_restarts_count(job_id, 1)

    # Verify SLURM_RESTART_COUNT=1 in output file
    check_restart_count_in_output(output_file, 1)
