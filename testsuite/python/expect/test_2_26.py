############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_config_parameter("PriorityWeightAge", 100)
    atf.require_config_parameter("PriorityWeightJobSize", 100)
    atf.require_config_parameter("PriorityWeightPartition", 100)
    atf.require_nodes(1)
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
