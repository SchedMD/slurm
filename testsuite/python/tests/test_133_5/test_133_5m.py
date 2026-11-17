##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    assert_restarts_count,
    check_ctl_state,
    check_dbd_states,
    check_hold,
    check_restart_count_in_output,
    cls1,
    cls2,
    cls3,
    fill_cluster,
    wait_for_revoked_on_origin,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow


def test_requeuehold_specialexit_running_sibling(
    federation_setup, cleanup, scripts, node_count
):
    """Test scontrol requeuehold state=specialexit of a running sibling job.

    Fill origin, submit to sibling. requeuehold state=specialexit.
    Verify SPECIAL_EXIT on origin. check_hold. Release.
    Verify PENDING all 3. Verify SLURM_RESTART_COUNT=1.
    """
    file_in = scripts["complete_script"]
    output_file = atf.module_tmp_path / "requeuehold_se_running_output"

    # Fill origin
    fill_cluster(cls1, scripts)

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output={output_file} --error=/dev/null"
        f" --requeue --open-mode=append {file_in}",
        fatal=True,
    )

    run_cluster = wait_for_running_cluster(job_id, f"{cls2},{cls3}")
    assert run_cluster in [cls2, cls3], "Job should run on sibling"

    # Wait for origin to see REVOKED
    wait_for_revoked_on_origin(job_id)

    # wait_for_revoked_on_origin only covers cls1; this also waits for the third sibling.
    atf.wait_for_job_state(job_id, "RUNNING", clusters=run_cluster, timeout=60)

    # Requeuehold with special exit
    atf.run_command(f"scontrol requeuehold state=specialexit {job_id}", fatal=True)

    # Verify SPECIAL_EXIT on origin
    atf.wait_for_job_state(
        job_id, "SPECIAL_EXIT", siblings=True, clusters=cls1, timeout=180, fatal=True
    )
    check_hold(job_id)
    check_ctl_state(job_id, cls1, "SPECIAL_EXIT")

    check_dbd_states(job_id, cls1, ["REVOKED", "PENDING"])

    # Release
    atf.run_command(f"scontrol release {job_id}", fatal=True)

    # With requeue_delay=0 the released job is eligible right away, so its
    # PENDING state is transient and it can already run in a free sibling.
    rerun_cluster = wait_for_running_cluster(job_id)
    assert rerun_cluster, "Requeued job should start running"
    assert (
        rerun_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"

    # Verify SLURM_RESTART_COUNT=1
    assert_restarts_count(job_id, 1)

    # Verify SLURM_RESTART_COUNT=1 in output file
    check_restart_count_in_output(output_file, 1)
