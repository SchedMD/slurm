############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_nodes(
        2, [("CPUS", 8), ("Sockets", 2), ("CoresPerSocket", 2), ("ThreadsPerCore", 2)]
    )
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
