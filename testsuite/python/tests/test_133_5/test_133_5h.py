##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    assert_restarts_count,
    check_restart_count_in_output,
    cls1,
    wait_for_running_cluster,
)

pytestmark = pytest.mark.slow


def test_requeue_restart_count(federation_setup, cleanup, scripts, node_count):
    """Test that SLURM_RESTART_COUNT increments after requeue."""
    file_in = scripts["restart_counter"]
    output_file = atf.module_tmp_path / "restart_count_output"

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output={output_file} --error=/dev/null"
        f" --requeue --open-mode=append --cluster-constraint=fa {file_in}",
        fatal=True,
    )

    c = wait_for_running_cluster(job_id)
    assert c == cls1, f"Job not running on {cls1}"

    # Requeue to increment SLURM_RESTART_COUNT
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)

    # With requeue_delay=0 the requeued job is eligible right away, so its
    # PENDING state is transient; the restart count is not.
    assert_restarts_count(job_id, 1)
    atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)

    # Verify SLURM_RESTART_COUNT=1 in output file
    check_restart_count_in_output(output_file, 1)
