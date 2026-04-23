############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import atf


def setup_module():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


def test_squeue_sluid():
    """Verify squeue SLUID format and filtering by job_id, SLUID, and mixed."""

    job_id = atf.submit_job_sbatch("-n1 --wrap 'srun sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid is not None, f"Job {job_id} has no SLUID"

    fmt = "--noheader -o '%.6i %.14s'"

    # squeue -j <job_id> prints the SLUID
    output = atf.run_command_output(f"squeue -j {job_id} {fmt}", fatal=True)
    assert re.search(
        rf"{job_id}\s+{re.escape(sluid)}", output
    ), f"Expected job_id and SLUID in output: {output}"

    # squeue -j <SLUID> finds the job
    output = atf.run_command_output(f"squeue -j {sluid} {fmt}", fatal=True)
    assert re.search(
        rf"{job_id}\s+{re.escape(sluid)}", output
    ), f"Expected job found by SLUID in output: {output}"

    # squeue -j <job_id>,<SLUID> (both refer to the same job)
    output = atf.run_command_output(f"squeue -j {job_id},{sluid} {fmt}", fatal=True)
    assert re.search(
        rf"{job_id}\s+{re.escape(sluid)}", output
    ), f"Expected job found by mixed ids in output: {output}"
