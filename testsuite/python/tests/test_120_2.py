############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import atf


def setup_module():
    atf.require_version(
        (26, 5),
        "bin/scontrol",
        reason="Ticket 25334: CgroupJobIdPaths parameter added in 26.05+",
    )
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_config_parameter_includes("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter("CgroupJobIdPaths", "yes", source="cgroup")
    atf.require_slurm_running()


def test_cgroup_path_contains_job_id():
    """Verify that a job step's cgroup path contains the numeric job ID.

    Expected cgroup line format:
      0::/.../job_<id>/step_0/user/task_0
    """

    file_out = atf.module_tmp_path / "cgroup_output"
    job_id = atf.submit_job_sbatch(
        f"-n1 --output={file_out} --wrap 'srun cat /proc/self/cgroup'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.wait_for_file(file_out, fatal=True)

    cgroup_output = atf.run_command_output(f"cat {file_out}", fatal=True)

    assert re.search(
        rf"/job_{job_id}/step_0/user/task_0", cgroup_output
    ), f"Expected job ID '{job_id}' in cgroup path, got: {cgroup_output}"
