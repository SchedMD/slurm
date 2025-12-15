############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import os
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    dummy_h5_dir = f"{atf.module_tmp_path}/h5"
    os.makedirs(dummy_h5_dir, exist_ok=True)
    atf.require_auto_config("Wants to create custom acct_gather.conf")
    atf.require_version((26, 5), component="bin/sh5util")
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_parameter(
        "ProfileHDF5Dir",
        parameter_value=dummy_h5_dir,
        source="acct_gather",
    )
    atf.require_config_parameter(
        "ProfileHDF5Default",
        parameter_value="ALL",
        source="acct_gather",
    )

    yield

    os.rmdir(dummy_h5_dir)


@pytest.mark.parametrize(
    "flag,value,msg",
    (
        ("--jobs", "", "option '--jobs' requires an argument"),
        ("--jobs", "1_0", "Job array IDs not supported"),
        ("--jobs", "1+0", "Het job IDs not supported"),
        ("--jobs", "1+", "Failed to parse job ID"),
        ("--jobs", "1a", "Failed to parse job ID"),
        ("--jobs", "a1", "Failed to parse job ID"),
        ("--jobs", "12345", "Merging node-step files into"),
    ),
)
def test_cli_parse(flag, value, msg):
    output = atf.run_command_error(f"sh5util {flag} {value}")
    assert msg in output
