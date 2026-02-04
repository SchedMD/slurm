##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import pytest

import atf

from .conftest import (
    assert_restarts_count,
    check_ctl_state,
    check_hold,
    check_restart_count_in_output,
    cls1,
    cls2,
    cls3,
    fill_cluster,
    wait_for_running_cluster,
    wait_for_sacct_state,
)

pytestmark = pytest.mark.slow


def test_requeuehold_specialexit_completed_sibling(
    federation_setup, cleanup, scripts, node_count
):
    """Test scontrol requeuehold state=specialexit of a completed sibling job.

    Fill origin, submit to sibling, wait for first run + completion.
    Requeuehold state=specialexit. Verify SPECIAL_EXIT. Release.
    Wait for re-run and completion. Requeuehold again.
    Verify SLURM_RESTART_COUNT=2.
    """
    file_in = scripts["complete_script"]
    output_file = atf.module_tmp_path / "requeuehold_se_completed_output"

    # Fill origin
    fill_cluster(cls1, scripts)

    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output={output_file} --error=/dev/null"
        f" --requeue --open-mode=append {file_in}",
        fatal=True,
    )

    run_cluster = wait_for_running_cluster(job_id, f"{cls2},{cls3}")
    assert run_cluster in [cls2, cls3], "Job should run on sibling"

    # Wait for completion
    wait_for_sacct_state(job_id, run_cluster, "COMPLETED", timeout=60)

    # Requeuehold with specialexit
    atf.run_command(f"scontrol requeuehold state=specialexit {job_id}", fatal=True)

    atf.wait_for_job_state(
        job_id, "SPECIAL_EXIT", siblings=True, clusters=cls1, timeout=180, fatal=True
    )
    check_hold(job_id)
    check_ctl_state(job_id, cls1, "SPECIAL_EXIT")

    # Release and wait for re-run
    atf.run_command(f"scontrol release {job_id}", fatal=True)

    # With requeue_delay=0 the released job is eligible right away, so its
    # PENDING state is transient and it can already run in a free sibling.
    rerun_cluster = wait_for_running_cluster(job_id)
    assert rerun_cluster, "Requeued job should start running"
    assert (
        rerun_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"

    # Wait for second completion (--duplicates needed to see multiple runs).
    # The first run is also in the records if the job reran on the same cluster.
    completed = 2 if rerun_cluster == run_cluster else 1

    def check_completed2():
        output = atf.run_command_output(
            f"sacct -o state -j {job_id} -M{rerun_cluster} --duplicates --parsable2 --noheader",
        )
        return output.count("COMPLETED") >= completed

    assert atf.repeat_until(
        check_completed2, lambda x: x, timeout=60
    ), f"Job {job_id} did not reach second COMPLETED on {rerun_cluster}"

    # Requeuehold state=specialexit again
    atf.run_command(f"scontrol requeuehold state=specialexit {job_id}", fatal=True)

    atf.wait_for_job_state(
        job_id, "SPECIAL_EXIT", siblings=True, clusters=cls1, timeout=180, fatal=True
    )
    check_hold(job_id)

    # Release and wait for third run
    atf.run_command(f"scontrol release {job_id}", fatal=True)

    # With requeue_delay=0 the released job is eligible right away, so its
    # PENDING state is transient and it can already run in a free sibling.
    run3_cluster = wait_for_running_cluster(job_id)
    assert run3_cluster, "Requeued job should start running"
    assert (
        run3_cluster != cls1
    ), "Requeued job should not run on origin (cls1 still full)"

    # Verify SLURM_RESTART_COUNT=2
    assert_restarts_count(job_id, 2)

    # Verify SLURM_RESTART_COUNT=2 in output file
    check_restart_count_in_output(output_file, 2)
