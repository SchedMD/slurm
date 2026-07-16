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


def test_expect():
    reason, rc = atf.run_expect_test(fail=False)

    if rc:
        if (
            atf.get_version() < (26, 5)
            and reason
            == "Failed: Subtest  1 failed  : Verify job is running after reconfiguration"
        ):
            pytest.xfail(f"Ticket 25126: {reason}. Fixed in 26.05.")
        else:
            pytest.fail(reason)
