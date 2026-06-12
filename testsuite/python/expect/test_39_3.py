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

    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        1,
        [
            ("CPUS", 32),
            ("Sockets", 2),
            ("CoresPerSocket", 8),
            ("ThreadsPerCore", 2),
            ("RealMemory", 2048),
            ("Gres", "gpu:1"),
        ],
    )

    atf.require_tty(0)
    atf.require_config_file(
        "gres.conf",
        "Name=gpu File=/dev/tty0",
    )

    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
