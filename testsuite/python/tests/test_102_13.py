############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test 102.13: sacctmgr show assoc withsubaccounts with partition assocs.

WithSubAccounts selects the associations under an account by matching the
account in their lineage. A partition-based association carries its
partition as the last lineage segment, so an association whose partition is
named like the requested account must not be selected (Ticket 23099).
"""

import re

import pytest

import atf

cluster1 = "test102_13_cluster"
parent_account = "test102_13_parent"
child_account = "test102_13_child"
partition_user = "test102_13_puser"
child_user = "test102_13_cuser"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # sacctmgr is probed rather than sreport because sreport -V exits before
    # parsing options unless accounting_storage/slurmdbd is configured.
    # Lower this gate to the first maintenance releases that carry the fix
    # once the backports land.
    atf.require_version(
        (26, 11),
        "bin/sacctmgr",
        reason="Ticket 23099: WithSubAccounts lineage matching fixed in 26.11",
    )
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def create_entities():
    """Populate the accounting database with the cluster, accounts and users"""

    # Clean up after an aborted earlier run; the removal is expected to fail
    # when nothing was left behind. Removing a cluster cascades a delete
    # across every per-cluster usage table, which can exceed the default
    # command timeout on a loaded DB.
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster1}",
        user=atf.properties["slurm-user"],
        timeout=120,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i add cluster {cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {parent_account} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {child_account} cluster={cluster1} parent={parent_account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # The partition is named like the child account, so this association's
    # lineage ends with "/<child_account>/" without being under it
    atf.run_command(
        f"sacctmgr -i add user user={partition_user} cluster={cluster1} account={parent_account} partition={child_account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user user={child_user} cluster={cluster1} account={child_account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    # The cascading delete can exceed the default command timeout
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster1}",
        user=atf.properties["slurm-user"],
        timeout=120,
        fatal=True,
    )
    # Users and accounts are not cluster-scoped, so removing the cluster
    # leaves them behind
    atf.run_command(
        f"sacctmgr -i remove user {partition_user},{child_user}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {parent_account},{child_account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module")
def subaccount_output(create_entities):
    """Return the withsubaccounts listing for the child account"""

    command = f"sacctmgr -n -P show assoc cluster={cluster1} account={child_account} withsubaccounts format=Account,User,Partition"

    return atf.run_command_output(command, fatal=True)


@pytest.fixture(scope="module")
def parent_subaccount_output(create_entities):
    """Return the withsubaccounts listing for the parent account"""

    command = f"sacctmgr -n -P show assoc cluster={cluster1} account={parent_account} withsubaccounts format=Account,User,Partition"

    return atf.run_command_output(command, fatal=True)


def test_sub_account_is_listed(parent_subaccount_output):
    """Requesting an account must also list its sub-account"""

    assert (
        re.search(rf"^{child_account}\|\|$", parent_subaccount_output, re.MULTILINE)
        is not None
    ), f"{child_account} should be listed under {parent_account}"


def test_sub_account_user_is_listed(parent_subaccount_output):
    """Requesting an account must also list a user in its sub-account"""

    assert (
        re.search(
            rf"^{child_account}\|{child_user}\|$",
            parent_subaccount_output,
            re.MULTILINE,
        )
        is not None
    ), f"{child_user} should be listed under {parent_account}"


def test_own_association_is_listed(subaccount_output):
    """The requested account's own association must be listed"""

    assert (
        re.search(rf"^{child_account}\|\|$", subaccount_output, re.MULTILINE)
        is not None
    ), f"{child_account} should list its own association"


def test_user_association_is_listed(subaccount_output):
    """A user association under the requested account must be listed"""

    assert (
        re.search(
            rf"^{child_account}\|{child_user}\|$", subaccount_output, re.MULTILINE
        )
        is not None
    ), f"{child_user} should be listed under {child_account}"


def test_partition_named_like_account_is_not_listed(subaccount_output):
    """An association whose partition is named like the requested account
    must not be listed"""

    assert (
        re.search(rf"^[^|]*\|{partition_user}\|", subaccount_output, re.MULTILINE)
        is None
    ), f"{partition_user} has a partition named {child_account}, not an association under it"
