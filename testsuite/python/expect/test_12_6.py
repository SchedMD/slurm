############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/linux")

    hdf5_dir = f"{atf.module_tmp_path}/profile"
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_file("acct_gather.conf", f"ProfileHDF5Dir={hdf5_dir}")
    atf.require_nodes(1)
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
