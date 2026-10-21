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
    atf.get_config_parameter(
        "TLSType", default="tls/none", live=False, quiet=True
    ).casefold()
    != "tls/none"
    and atf.get_version("bin/srun") < (25, 11),
    reason="Ticket 23273: contribs/pmi can't connect to srun with TLS. Fixed in 25.11",
)
def test_expect():
    atf.run_expect_test()
