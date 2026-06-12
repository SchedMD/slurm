############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_config_parameter("MinJobAge", 10)
    atf.require_nodes(4)
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
