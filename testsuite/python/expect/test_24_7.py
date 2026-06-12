############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
