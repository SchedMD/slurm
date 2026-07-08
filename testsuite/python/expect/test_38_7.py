############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    # Just to make the test faster
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))

    atf.require_nodes(5, [("CPUs", 2)])
    atf.require_slurm_running()

    atf.require_lmod()
    atf.module_load("openmpi")


def test_expect():
    atf.run_expect_test()
