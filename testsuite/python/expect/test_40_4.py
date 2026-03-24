############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_config_parameter_includes("AccountingStorageTres", "gres/mps")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "mps")
    atf.require_nodes(1, [("CPUs", 1), ("Gres", "gpu:1,mps:100")])

    atf.require_tty(0)
    atf.require_config_file(
        "gres.conf",
        "Name=gpu File=/dev/tty0",
    )
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
