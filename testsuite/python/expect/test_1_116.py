############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_nodes(1)
    atf.require_slurm_running()

    atf.require_lmod()
    atf.module_load("openmpi")


def test_expect():
    atf.run_expect_test()
