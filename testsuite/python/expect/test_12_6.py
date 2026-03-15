############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    # TODO: Why is this test failing?
    hdf5_dir = f"{atf.module_tmp_path}/profile"
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_file("acct_gather.conf", f"ProfileHDF5Dir={hdf5_dir}")
    atf.require_nodes(1)
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
