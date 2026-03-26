############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("DefMemPerNode", 24)
    atf.require_nodes(
        5,
        [
            ("CPUS", 32),
            ("Sockets", 2),
            ("CoresPerSocket", 8),
            ("ThreadsPerCore", 2),
            ("RealMemory", 1024),
        ],
    )
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
