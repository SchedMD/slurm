############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_config_parameter("AccountingStorageTres", "gres/gpu")

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")

    atf.require_config_parameter("GresTypes", "gpu")
    atf.require_nodes(1, [("CPUs", 4), ("Gres", "gpu:2")])

    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_file(
        "gres.conf",
        "Name=gpu File=/dev/tty[0-1]",
    )

    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
