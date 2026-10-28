############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Profile columns stay zero for the extern step's never-sampled task.

The extern step registers its only task with pid 0, which the accounting
poll matches to no process record, so every usage total behind a Task
profile column keeps the INFINITE64 no-sample sentinel. With
ProfileHDF5Default=Task the step profiles that task anyway. Before slurmd
26.11 its samples carried the sentinel into the RSS and VMSize columns as
18014398509481983 (INFINITE64 divided by 1024) and into Pages as
18446744073709551615, left GPUMemMB unwritten, and left GPUUtilization
unwritten after the first sample. Since 26.11 every column reads 0. No GPU
TRES is tracked here, so the module also pins the GPU columns at 0 under the
default AccountingStorageTRES. The batch step of the same job is sampled
normally and keeps a nonzero RSS. The extern step's task is polled only when
its tracking cgroup lists the slurmstepd itself, which proctrack/cgroup does
under cgroup/v2, so both are pinned along with PrologFlags Contain. No GPU
hardware is needed.
"""

import logging

import pytest

import atf
import hdf5_profile

profile_cols = [
    "Step",
    "CPUFrequency",
    "CPUTime",
    "CPUUtilization",
    "GPUMemMB",
    "GPUUtilization",
    "RSS",
    "VMSize",
    "Pages",
    "ReadMB",
    "WriteMB",
]
gpu_cols = ["GPUMemMB", "GPUUtilization"]


@pytest.fixture(scope="module", autouse=True)
def setup(hdf5_dir):
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="MR 4338: the profile records an unsampled TRES as zero since 26.11",
    )
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_parameter("ProfileHDF5Dir", hdf5_dir, source="acct_gather")
    # Profile every step's tasks by default, the extern step's included.
    atf.require_config_parameter("ProfileHDF5Default", "Task", source="acct_gather")
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter("JobAcctGatherFrequency", "task=2")
    # Keep the GPU TRES untracked, so the GPU columns come from the zeroed
    # sample rather than from a mapped usage slot.
    atf.require_config_parameter("AccountingStorageTRES", None)
    # The extern step's task is polled only when its tracking cgroup lists
    # the slurmstepd itself, which proctrack/cgroup does under cgroup/v2.
    atf.require_config_parameter("ProctrackType", "proctrack/cgroup")
    atf.require_config_parameter("CgroupPlugin", "cgroup/v2", source="cgroup")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def hdf5_dir():
    return hdf5_profile.profile_hdf5_dir()


@pytest.fixture(scope="module")
def user_profile_dir(hdf5_dir):
    return hdf5_profile.profile_user_dir(hdf5_dir)


@pytest.fixture(scope="module")
def job_id(user_profile_dir):
    """A batch job sleeping long enough for several task profile samples.

    A job reported DONE has ended its extern step as well, so both node step
    profile files are complete once the fixture yields.
    """
    job_id = atf.submit_job_sbatch(
        '--job-name=test_149_6 -N1 -n1 -t1 --wrap "sleep 12"', fatal=True
    )
    # The extern step's teardown can keep the job COMPLETING well past the
    # sleep on a loaded node, so allow more than the default 45 seconds.
    atf.wait_for_job_state(job_id, "DONE", timeout=120, fatal=True)

    yield job_id

    atf.run_command(f"rm -f {user_profile_dir}/{job_id}_*.h5", fatal=True, quiet=True)


@pytest.fixture(scope="module")
def rows(user_profile_dir, job_id):
    hdf5_profile.wait_for_node_profiles(user_profile_dir, job_id, 2)
    hdf5_file = hdf5_profile.merge_job_profiles(job_id)
    return hdf5_profile.read_sh5util_csv(
        job_id, hdf5_file, "-E --level=Node:TimeSeries", profile_cols
    )


def test_extern_task_columns_zero(rows):
    """Every extern step sample reads 0 in every column.

    The batch step of the same job keeps a sampled RSS and reads 0 in the GPU
    columns, which no tracked TRES feeds.
    """
    steps = sorted({row["Step"] for row in rows})
    logging.info(f"Profiled steps: {steps}")
    extern_rows = [row for row in rows if row["Step"] != "batch"]
    batch_rows = [row for row in rows if row["Step"] == "batch"]
    assert extern_rows, f"Expected extern step samples among steps {steps}"
    assert batch_rows, f"Expected batch step samples among steps {steps}"
    assert any(
        int(row["RSS"]) > 0 for row in batch_rows
    ), f"The batch step task was never sampled: {batch_rows}"
    for row in batch_rows:
        for col in gpu_cols:
            assert (
                float(row[col]) == 0
            ), f"Nonzero {col} sample in the batch step: {row}"
    for row in extern_rows:
        for col in profile_cols[1:]:
            assert (
                float(row[col]) == 0
            ), f"Nonzero {col} sample in the extern step: {row}"
