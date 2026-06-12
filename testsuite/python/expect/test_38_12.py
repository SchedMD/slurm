############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    # Just to make the test faster
    atf.require_config_parameter_includes("SchedulerParameters", "bf_interval=1")

    atf.require_nodes(3)
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
