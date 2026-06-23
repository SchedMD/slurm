############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_nodes(4)
    atf.require_slurm_running()


def test_expect():
    reason, rc = atf.run_expect_test(fail=False)
    if rc:
        if (
            atf.get_version("sbin/slurmd") < (26, 5, 2)
            and reason == "Wrong node responded, expected node 0"
        ):
            pytest.xfail(f"Ticket 24877: {reason}. Fixed in 26.05.2")
        else:
            pytest.fail(reason)
