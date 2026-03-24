############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    # Just to make the test faster
    atf.require_config_parameter_includes("SchedulerParameters", "bf_interval=1")

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")

    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(2, [("CPUs", 1), ("Gres", "gpu:1")])

    atf.require_tty(0)
    atf.require_config_file(
        "gres.conf",
        "Name=gpu File=/dev/tty0",
    )

    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
