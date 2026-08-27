############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Profile GPU columns stay zero when only gres/gpumem is tracked.

With AccountingStorageTRES=gres/gpumem and no gres/gpu entry the
gres/gpuutil auto-append stays off, so slurmstepd resolves the gpumem
usage TRES position while the gpuutil position stays unresolved. The
profile dataset still defines both GPU columns, and since slurmd 26.11
each is filled behind its own TRES position guard, with
JobAcctGatherParams=DisableGPUAcct and the cgroup device memory accounting
off leaving the gpumem slot at the INFINITE64 no-sample sentinel whether
or not the node has GPUs. Before the guard split the GPUUtilization column
was read through the gpumem position guard alone, an out-of-bounds read of
the step usage array here, showing as nonzero GPUUtilization samples. The
gres TRES require slurmdbd, so the module needs accounting and no GPU
hardware.
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
    # Track only the gpumem usage TRES, with no gres/gpu entry so the
    # gres/gpuutil auto-append stays off and the gpuutil position stays
    # unresolved. The gres TRES require slurmdbd, so bring accounting up
    # first.
    atf.require_accounting(modify=True)
    atf.require_config_parameter("AccountingStorageTRES", "gres/gpumem")
    # Keep GPU usage samples out of the gpumem usage slot so it holds the
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
    yield from hdf5_profile.run_profile_job(user_profile_dir, "test_149_5")


@pytest.fixture(scope="module")
def node_profiles(user_profile_dir, job_id):
    return hdf5_profile.wait_for_node_profiles(user_profile_dir, job_id, 1)


@pytest.fixture(scope="module")
def hdf5_file(job_id, node_profiles):
    return hdf5_profile.merge_job_profiles(job_id)


def test_gpu_columns_zero_with_gpumem_alone(job_id, hdf5_file):
    """GPUMemMB and GPUUtilization are exactly 0 in every profile sample.

    The dataset defines both columns unconditionally, so every sample
    carries both even though only gres/gpumem is tracked. DisableGPUAcct,
    with ConstrainDeviceMemory off, leaves the gpumem step usage slot at
    the INFINITE64 no-sample sentinel, which
    hdf5_profile.assert_gpu_columns_zero requires the profile to record as
    0, and the per-TRES guard must leave the untracked GPUUtilization
    column at exactly 0.0 instead of reading an out-of-bounds usage slot.
    """

    rows = hdf5_profile.read_sh5util_csv(
        job_id, hdf5_file, "-E --level=Node:TimeSeries", gpu_cols
    )

    hdf5_profile.assert_gpu_columns_zero(rows)
