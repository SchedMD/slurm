############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_config_parameter("TaskPlugin", "affinity")
    atf.require_nodes(1, [("CPUs", 2)])

    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
