############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import atf


def setup_module():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_config_parameter_includes("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_slurm_running()


def test_cgroup_path_contains_sluid():
    """Verify that a job step's cgroup path contains the job's SLUID.

    Expected cgroup line format:
      0::/.../<sluid>/step_0/user/task_0
    """

    file_out = atf.module_tmp_path / "cgroup_output"
    job_id = atf.submit_job_sbatch(
        f"-n1 --output={file_out} --wrap 'srun cat /proc/self/cgroup'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid is not None, f"Job {job_id} has no SLUID"

    atf.wait_for_file(file_out, fatal=True)

    cgroup_output = atf.run_command_output(f"cat {file_out}", fatal=True)

    assert re.search(
        rf"/{re.escape(sluid)}/step_0/user/task_0", cgroup_output
    ), f"Expected SLUID '{sluid}' in cgroup path, got: {cgroup_output}"
