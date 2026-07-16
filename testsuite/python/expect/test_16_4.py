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


@pytest.mark.xfail(
    atf.get_version("bin/sattach") < (25, 5)
    and (25, 5) <= atf.get_version("sbin/slurmd")
    and atf.get_version("sbin/slurmd") < (26, 5),
    reason="Ticket 25103: sattach 24.11 doesn't attach properly to newer slurmd when using '-l --output-filter'. Fixed in slurmd 26.05+",
)
def test_expect():
    atf.run_expect_test()
