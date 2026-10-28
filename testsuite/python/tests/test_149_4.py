############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Profile GPU columns stay zero for a step without GPU usage samples.

With gres/gpu, gres/gpumem, and gres/gpuutil tracked in
AccountingStorageTRES, slurmstepd resolves both GPU usage TRES positions
and fills the GPUMemMB and GPUUtilization profile columns from the step
usage slots, which JobAcctGatherParams=DisableGPUAcct, with the cgroup
device memory accounting off, leaves at the INFINITE64 no-sample sentinel
whether or not the node has GPUs. Before slurmd 26.11 that sentinel
reached the dataset as a GPUMemMB sample of 17592186044415 (INFINITE64
divided by 1048576). The gres TRES require slurmdbd, so the module needs
accounting and no GPU hardware.
"""

import pytest

import atf
import hdf5_profile

gpu_cols = ["GPUMemMB", "GPUUtilization"]


@pytest.fixture(scope="module", autouse=True)
def setup(hdf5_dir):
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="MR 4338: the profile records unsampled GPU TRES as zero since 26.11",
    )
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_parameter(
        "ProfileHDF5Dir",
        hdf5_dir,
        source="acct_gather",
    )
    atf.require_config_parameter(
        "JobAcctGatherType",
        "jobacct_gather/cgroup",
    )
    # Track the GPU usage TRES so the profile emits both GPU columns from
    # the step usage slots. The gres TRES require slurmdbd, so bring
    # accounting up first.
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpumem")
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpuutil")
    # Keep GPU usage samples out of the step usage slots so they hold the
    # no-sample sentinel deterministically, with or without GPUs on the node.
    atf.require_config_parameter_includes("JobAcctGatherParams", "DisableGPUAcct")
    # The cgroup plugin fills the gpumem slot from the dmem controller when
    # ConstrainDeviceMemory is on, DisableGPUAcct notwithstanding.
    atf.require_config_parameter("ConstrainDeviceMemory", [None, "no"], source="cgroup")

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def hdf5_dir():
    return hdf5_profile.profile_hdf5_dir()


@pytest.fixture(scope="module")
def user_profile_dir(hdf5_dir):
    return hdf5_profile.profile_user_dir(hdf5_dir)


@pytest.fixture(scope="module")
def job_id(user_profile_dir):
    yield from hdf5_profile.run_profile_job(user_profile_dir, "test_149_4")


@pytest.fixture(scope="module")
def node_profiles(user_profile_dir, job_id):
    return hdf5_profile.wait_for_node_profiles(user_profile_dir, job_id, 1)


@pytest.fixture(scope="module")
def hdf5_file(job_id, node_profiles):
    return hdf5_profile.merge_job_profiles(job_id)


def test_gpu_columns_zero_without_gpu_samples(job_id, hdf5_file):
    """GPUMemMB and GPUUtilization are exactly 0 in every profile sample.

    Both GPU usage TRES are tracked, so slurmstepd fills both columns of
    every sample from the step usage slots, and DisableGPUAcct, with
    ConstrainDeviceMemory off, leaves those slots at the INFINITE64 no-sample
    sentinel, which hdf5_profile.assert_gpu_columns_zero requires the profile
    to record as 0.
    """

    rows = hdf5_profile.read_sh5util_csv(
        job_id, hdf5_file, "-E --level=Node:TimeSeries", gpu_cols
    )

    hdf5_profile.assert_gpu_columns_zero(rows)
