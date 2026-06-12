############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_nodes(3, [("CPUs", 8)])
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i create user {atf.properties['slurm-user']} account=root",
        fatal=True,
        user=atf.properties["slurm-user"],
    )


def test_expect():
    atf.run_expect_test()
