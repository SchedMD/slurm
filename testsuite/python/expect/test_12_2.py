############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_nodes(1, [("CPUS", 4), ("RealMemory", 1024)])
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
