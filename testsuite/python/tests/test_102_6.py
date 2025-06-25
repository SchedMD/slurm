############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

# Global variables
cluster1 = "cluster1"
cluster2 = "cluster2"
cluster3 = "cluster3"
acct1 = "acct1"
acct2 = "acct2"
acct3 = "acct3"
acct4 = "acct4"
user1 = "user1"
defacct = {acct1}


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("AllowNoDefAcct", None, source="slurmdbd")
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    # Create test clusters and accounts
    atf.run_command(
        f"sacctmgr -i add cluster {cluster1},{cluster2},{cluster3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct1},{acct2},{acct3},{acct4} cluster={cluster1},{cluster2},{cluster3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user1} DefaultAccount={defacct} account={acct1},{acct2},{acct3},{acct4} cluster={cluster1},{cluster2},{cluster3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield

    atf.run_command(
        f"sacctmgr -i remove user {user1} {acct1},{acct2},{acct3},{acct4}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {acct1},{acct2},{acct3},{acct4}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster1},{cluster2},{cluster3}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_account_removal():
    """Test removing an account:
    - Verify that removing default account for a user fails.
    """

    # Try to remove a user's default account
    atf.run_command(
        f"sacctmgr -i remove account {defacct} cluster={cluster1},{cluster2},{cluster3}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )

    # Try to remove subset (including default) of accounts of user on cluster1
    atf.run_command(
        f"sacctmgr -i remove account {defacct},{acct2} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )

    # Make user have only one account, the default on cluster1
    atf.run_command(
        f"sacctmgr -i remove account {acct2},{acct3},{acct4} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Try to remove default account on all clusters
    atf.run_command(
        f"sacctmgr -i remove account {defacct}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )

    # Make acct2 have acct1 as parent
    atf.run_command(
        f"sacctmgr -i modify account {acct2} cluster={cluster1},{cluster2},{cluster3} set parent={acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Again, try to remove default account on all clusters
    atf.run_command(
        f"sacctmgr -i remove account {defacct}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )

    # Remove all accounts of user on cluster2
    atf.run_command(
        f"sacctmgr -i remove user {user1} {acct2},{acct3},{acct4} cluster={cluster2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Remove accounts acct2,acct3,acct4 from user on remaining clusters
    atf.run_command(
        f"sacctmgr -i remove user {user1} {acct2},{acct3},{acct4}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Again, try to remove default account on all clusters since it is now user's only account
    atf.run_command(
        f"sacctmgr -i remove account {defacct}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
