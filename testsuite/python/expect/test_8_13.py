############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_accounting()
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")

    # TODO: Remove extra slurmdbd logging once Ticket 23621 is resolved.
    atf.require_config_parameter_includes("DebugFlags", "DB_Assoc", source="slurmdbd")
    atf.require_config_parameter("DebugLevel", "debug2", source="slurmdbd")

    atf.require_nodes(1)
    atf.require_slurm_running()
    atf.run_command(
        f"sacctmgr -i create user {atf.properties['slurm-user']} account=root",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_expect():
    # TODO: Remove fail=False and saving the DB once Ticket 23621 is fixed
    reason, rc = atf.run_expect_test(fail=False)
    if rc:
        if atf.properties["auto-config"]:
            atf.dump_accounting_database(
                f"{atf.properties['slurm-logs-dir']}/slurm_acct_db_t23621.sql.gz"
            )
        pytest.fail(f"{reason}. DB dumped for t23621.")
