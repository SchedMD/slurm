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
    atf.require_nodes(4, [("CPUs", 4)])
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i create user {atf.properties['slurm-user']} account=root",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_expect():
    atf.run_expect_test()
