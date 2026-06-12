############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

# Global variables
cluster1 = "cluster1"
cluster2 = "cluster2"
cluster3 = "cluster3"
acct1 = "acct1"
acct2 = "acct2"
acct3 = "acct3"
acct4 = "acct4"
acct5 = "acct4_sub"
acct6 = "acct4_sub_sub"
acct7 = "acct5"
acct8 = "acct6"
acct9 = "acct7"
user1 = "user1"
user2 = "user2"
user3 = "user3"
part1 = "part1"
part2 = "part2"
part3 = "part3"
part4 = "part4"
defacct = acct1


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("AllowNoDefAcct", "No", source="slurmdbd")
    # these can be helpful when debugging/tracing
    # atf.require_config_parameter("DebugLevel", "debug4", source="slurmdbd")
    # atf.require_config_parameter("DebugFlags", f"{atf.get_config(live=False, source="slurmdbd", quiet=True)["DebugFlags"]},DB_ASSOC", source="slurmdbd")
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
        f"sacctmgr -i remove user {user1} acct={acct1},{acct2},{acct3},{acct4}",
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
    result = atf.run_command(
        f"sacctmgr -i remove account {defacct} cluster={cluster1},{cluster2},{cluster3}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert result["exit_code"] != 0, "Removing default account should fail"
    assert (
        "can not remove the default account of a user" in result["stderr"]
    ), 'sacctmgr should explain that we "can not remove the default account of a user"'

    # Try to remove subset (including default) of accounts of user on cluster1
    result = atf.run_command(
        f"sacctmgr -i remove account {defacct},{acct2} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), "Removing default account, either with other accounts, should fail"
    assert (
        "can not remove the default account of a user" in result["stderr"]
    ), 'sacctmgr should explain that we "can not remove the default account of a user"'

    # Make user have only one account, the default on cluster1
    atf.run_command(
        f"sacctmgr -i remove account {acct2},{acct3},{acct4} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Try to remove default account on all clusters
    result = atf.run_command(
        f"sacctmgr -i remove account {defacct}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), "Removing default account when it's the only one account should fail"
    assert (
        "can not remove the default account of a user" in result["stderr"]
    ), 'sacctmgr should explain that we "can not remove the default account of a user"'

    # Make acct2 have acct1 as parent
    atf.run_command(
        f"sacctmgr -i modify account {acct2} cluster={cluster1},{cluster2},{cluster3} set parent={acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Again, try to remove default account on all clusters
    result = atf.run_command(
        f"sacctmgr -i remove account {defacct}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        result["exit_code"] != 0
    ), "Removing default account, either when it's a parent, should fail"
    assert (
        "can not remove the default account of a user" in result["stderr"]
    ), 'sacctmgr should explain that we "can not remove the default account of a user"'

    # Remove all accounts of user on cluster2
    atf.run_command(
        f"sacctmgr -i remove user {user1} acct={acct2},{acct3},{acct4} cluster={cluster2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Remove accounts acct2,acct3,acct4 from user on remaining clusters
    atf.run_command(
        f"sacctmgr -i remove user {user1} acct={acct2},{acct3},{acct4}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Again, try to remove default account on all clusters since it is now user's only account
    result = atf.run_command(
        f"sacctmgr -i remove account {defacct}",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] == 0
    ), "Removing default account when it's not a default account should succeed"
    assert defacct in result["stdout"], "sacctmgr should report the deleted account"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (24, 11, 4)
    or (
        (25, 5) <= atf.get_version("sbin/slurmdbd")
        and atf.get_version("sbin/slurmdbd") < (25, 11)
    ),
    reason="Ticket 24228: In 24.11.4 we fixed an issue when removing an account specifying the parent",
)
def test_account_removal_with_parent():
    """Test removing an account using parent="""

    # Add a sub acct
    atf.run_command(
        f"sacctmgr -i add account {acct5} parent={acct4} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Add another sub acct
    atf.run_command(
        f"sacctmgr -i add account {acct6} parent={acct5} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Now add user to it
    atf.run_command(
        f"sacctmgr -i add user {user1} acct={acct5} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Remove non-default account with parent specified
    result = atf.run_command(
        f"sacctmgr -i remove account {acct5} parent={acct4} cluster={cluster1}",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] == 0
    ), "Removing non-default account with a parent should succeed"
    assert (
        f"A = {acct5}" in result["stdout"]
    ), "sacctmgr should report the deleted account"

    # Confirm previous operation succeeded
    output = atf.run_command_output(
        f"sacctmgr -n show assoc format=acct,user,lineage acct={acct5},{acct6} cluster={cluster1}",
        fatal=True,
    )
    assert (
        output == ""
    ), f"User {user1} with acct {acct5} shouldn't have an assoc anymore"


def test_account_removal_with_partition():
    """Test removing an account using partition="""

    # Add user to non-def account with partition
    atf.run_command(
        f"sacctmgr -i add user {user1} acct={acct4} partition={part1} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Remove account with partition specified
    result = atf.run_command(
        f"sacctmgr -i remove acct {acct4} partition={part1} cluster={cluster1}",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, "Removing account with a partition should succeed"

    # Confirm previous operation succeeded
    output = atf.run_command_output(
        f"sacctmgr -n show assoc format=acct,user,lineage acct={acct4} partition={part1} cluster={cluster1}",
        fatal=True,
    )
    assert (
        output == ""
    ), f"Account {acct4} with partition {part1} shouldn't have an assoc anymore"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (25, 11, 6),
    reason="Ticket 24442: In 24.11.6+ we allow a user default account to be removed if more than one set",
)
def test_add_del_user_def_acct():
    """Test adding multiple default accounts"""

    # Add account
    atf.run_command(
        f"sacctmgr -i add acct {acct7} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Add user with default account (no partition)
    atf.run_command(
        f"sacctmgr -i add user {user2} defaultaccount={acct7} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Add user with default account and partition
    atf.run_command(
        f"sacctmgr -i add user {user2} defaultaccount={acct7} partition={part1} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Confirm there are two defaults
    output = atf.run_command_output(
        f"sacctmgr -n -P show assoc where user={user2} format=acct onlydef",
        fatal=True,
    )
    assert (
        len(re.findall(rf"{acct7}", output, re.IGNORECASE)) == 2
    ), f"User {user2} should have more than one default assoc"

    # Confirm the last default added should be able to be removed
    result = atf.run_command(
        f"sacctmgr -i del user {user2} acct={acct7} partition={part1} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        result["exit_code"] == 0
    ), f"Should be able to remove acct {acct7} for user {user2} with partition {part1}"

    # Add back in the default account (with partition)
    atf.run_command(
        f"sacctmgr -i add user {user2} defaultaccount={acct7} partition={part1} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Confirm removing user assoc with default account AND no partition succeeds
    result = atf.run_command(
        f'sacctmgr -i del user {user2} acct={acct7} partition=\\"\\" cluster={cluster1}',
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        result["exit_code"] == 0
    ), f"Should be able to remove acct {acct7} for user {user2} with empty partition"


def test_change_user_def_acct():
    """Test changing default account for user"""

    # Add two accts
    atf.run_command(
        f"sacctmgr -i add acct {acct8} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add acct {acct9} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Add user with default account and partition
    atf.run_command(
        f"sacctmgr -i add user {user3} defaultaccount={acct8} partition={part1} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Add user with another default account and partition
    atf.run_command(
        f"sacctmgr -i add user {user3} defaultaccount={acct8} partition={part2} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Add user with default account and no partition
    atf.run_command(
        f"sacctmgr -i add user {user3} defaultaccount={acct8} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Add user with a non-def account and partition
    atf.run_command(
        f"sacctmgr -i add user {user3} account={acct9} partition={part3} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Add user with a non-def account and another partition
    atf.run_command(
        f"sacctmgr -i add user {user3} account={acct9} partition={part4} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Now change def account
    result = atf.run_command(
        f"sacctmgr -i mod user {user3} cluster={cluster1} set defaultaccount={acct9}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        result["exit_code"] == 0
    ), f"Should be able to set new default account {acct9} for user {user3}"

    # Confirm total assocs for user
    output = atf.run_command_output(
        f"sacctmgr -n -P show assoc where user={user3} format=acct,partition",
        fatal=True,
    )
    assert (
        len(re.findall(rf"{acct8}", output, re.IGNORECASE)) == 3
    ), f"User {user3} should have 3 assocs for acct {acct8}"
    assert (
        len(re.findall(rf"{acct9}", output, re.IGNORECASE)) == 2
    ), f"User {user3} should have 5 assocs for acct {acct9}"

    # Confirm there are two defaults with partitions
    output = atf.run_command_output(
        f"sacctmgr -n -P show assoc where user={user3} format=acct,partition onlydef",
        fatal=True,
    )
    assert (
        len(re.findall(rf"{acct9}\|part[0-9]", output, re.IGNORECASE)) == 2
    ), f"User {user3} should have 2 default assocs with partitions"
