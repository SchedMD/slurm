############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Basic heterogeneous job sbatch test.

Submit a 3-component hetjob via sbatch and verify that the batch script
sees the expected per-component environment (SLURM_HET_SIZE, the
SLURM_JOB_ID_HET_GROUP_<N> / SLURM_NTASKS_HET_GROUP_<N> /
SLURM_CPUS_PER_TASK_HET_GROUP_<N> / SLURM_MEM_PER_CPU_HET_GROUP_<N> vars,
and a per-component partition var for each group).
"""

import re

import pytest

import atf

# (cpus-per-task, mem-per-cpu, ntasks) per het group. The values differ
# between components so that broadcasting one component's value across
# every _HET_GROUP_<N> suffix would be caught.
COMPONENTS = [(4, 10, 1), (2, 2, 2), (1, 6, 3)]

# Echoed after the env dump. The env output order is not deterministic, so
# this marker is what tells us the whole dump has been flushed.
OUTPUT_MARKER = "HETJOB_ENV_DUMPED"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(3, [("CPUs", 4), ("RealMemory", 1024)])
    # Het jobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_slurm_running()


def test_hetjob_sbatch_env():
    """Verify hetjob env vars in the batch script."""

    file_in = "hetjob.in"
    file_out = "hetjob.out"
    directives = "\n#SBATCH hetjob\n".join(
        f"#SBATCH --cpus-per-task={cpus} --mem-per-cpu={mem} --ntasks={ntasks} -t1"
        for cpus, mem, ntasks in COMPONENTS
    )
    atf.make_bash_script(file_in, f"{directives}\n\nenv\necho {OUTPUT_MARKER}\n")

    leader_job_id = atf.submit_job_sbatch(f"-o {file_out} {file_in}", fatal=True)

    jobs = atf.get_jobs(leader_job_id, fatal=True)
    component_ids = sorted(
        atf.range_to_list(jobs[leader_job_id]["HetJobIdSet"]),
        key=lambda job_id: jobs[job_id]["HetJobOffset"],
    )

    atf.wait_for_job_state(leader_job_id, "DONE", fatal=True)
    atf.assert_file_contents(file_out, OUTPUT_MARKER, contains=True)

    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    assert (
        re.search(r"^SLURM_HET_SIZE=3$", output, re.MULTILINE) is not None
    ), "Missing SLURM_HET_SIZE=3 in batch environment"

    for offset, (cpus, mem, ntasks) in enumerate(COMPONENTS):
        expected = {
            "SLURM_JOB_ID": component_ids[offset],
            "SLURM_CPUS_PER_TASK": cpus,
            "SLURM_MEM_PER_CPU": mem,
            "SLURM_NTASKS": ntasks,
        }
        for base, value in expected.items():
            var = f"{base}_HET_GROUP_{offset}"
            assert (
                re.search(rf"^{var}={value}$", output, re.MULTILINE) is not None
            ), f"Missing {var}={value} in batch environment"

    partition_offsets = sorted(
        int(offset)
        for offset in re.findall(
            r"^SLURM_JOB_PARTITION_HET_GROUP_(\d+)=", output, re.MULTILINE
        )
    )
    assert partition_offsets == [0, 1, 2], (
        "Expected SLURM_JOB_PARTITION_HET_GROUP_ vars for offsets [0, 1, 2],"
        f" got {partition_offsets}"
    )
