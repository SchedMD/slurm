############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "safe")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_nodes(
        2,
        [
            ("CPUS", 32),
            ("Sockets", 2),
            ("CoresPerSocket", 8),
            ("ThreadsPerCore", 2),
            ("RealMemory", 2048),
        ],
    )
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i create user {atf.properties['slurm-user']} account=root",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_expect():
    atf.run_expect_test()
