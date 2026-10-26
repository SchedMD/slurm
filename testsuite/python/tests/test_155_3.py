############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sh5util Network fields (--profile=network)."""

import re
from pathlib import Path

import pytest

import atf

network_fields = [
    "ElapsedTime",
    "EpochTime",
    "PacketsIn",
    "PacketsOut",
    "InMB",
    "OutMB",
]


@pytest.fixture(scope="module", autouse=True)
def setup(hdf5_dir):
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_parameter(
        "AcctGatherInterconnectType", "acct_gather_interconnect/sysfs"
    )
    atf.require_config_parameter(
        "ProfileHDF5Dir",
        hdf5_dir,
        source="acct_gather",
    )
    # The loopback interface always exists
    atf.require_config_parameter_includes("SysfsInterfaces", "lo", source="acct_gather")

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def hdf5_dir():
    return Path("h5").absolute()


@pytest.fixture(scope="module")
def user_profile_dir(hdf5_dir):
    return hdf5_dir / atf.properties["test-user"]


@pytest.fixture(scope="module")
def job_id(user_profile_dir):
    """Generate a network profile and return the job id"""

    job_id = atf.submit_job_srun(
        "--acctg-freq=network=1 --profile=network -N1 sleep 4",
        fatal=True,
    )

    yield job_id

    atf.run_command(
        f"rm -f {user_profile_dir}/{job_id}_*.h5",
        quiet=True,
    )


@pytest.fixture(scope="module")
def hdf5_file(user_profile_dir, job_id):
    hdf5_file = Path("test.h5").absolute()

    for t in atf.timer(fatal=True):
        if atf.run_command_output(
            f"ls -1 {user_profile_dir}/{job_id}_*.h5", quiet=True
        ).strip():
            break

    atf.run_command(f"sh5util -j {job_id} -o {hdf5_file}", fatal=True)

    return hdf5_file


def test_dataset_path(job_id, hdf5_file):
    """Network data is stored at the documented dataset path"""

    atf.require_tool("h5dump")

    nodes = atf.node_range_to_list(
        atf.get_job_parameter(job_id, "NodeList", fatal=True)
    )
    assert len(nodes) == 1, f"Expected one job node, saw: {nodes}"

    output = atf.run_command_output(f"h5dump -n {hdf5_file}", fatal=True)
    dataset = f"/Steps/0/Nodes/{nodes[0]}/Network"
    assert re.search(
        rf"^\s*dataset\s+{re.escape(dataset)}$", output, re.MULTILINE
    ), f"Missing {dataset} dataset in {hdf5_file}"


def test_listed_fields(job_id, hdf5_file):
    """Network series lists the documented fields"""

    output = atf.run_command_output(
        f"sh5util -i {hdf5_file} -j {job_id} -s Network -L",
        fatal=True,
    )
    missing = set(network_fields) - set(output.strip().splitlines())
    assert not missing, f"Missing Network field(s): {missing}"


def test_extract_header(job_id, hdf5_file):
    """Extracted Network time series has the documented columns"""

    output_file = "output"
    atf.run_command(
        f"sh5util -i {hdf5_file} -o {output_file} -j {job_id} -s Network "
        "-E --level=Node:TimeSeries",
        fatal=True,
    )

    header = atf.run_command_output(f"head -n 1 {output_file}", fatal=True)
    missing = set(network_fields) - set(header.strip().split(","))
    assert not missing, f"Missing Network CSV column(s): {missing}"
