############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")
    atf.require_config_parameter("GresTypes", "craynetwork")
    atf.require_nodes(2, [("CPUs", 2), ("Gres", "craynetwork:1")])

    atf.require_config_file(
        "gres.conf",
        "Name=craynetwork Count=2",
    )
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
