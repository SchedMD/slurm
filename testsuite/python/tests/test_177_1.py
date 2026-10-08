############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test PrivateData=users in slurmdbd.conf."""

import pytest

import atf

acct = "test-acct-177-1"
coord_acct = f"{acct}-2"
other_acct = f"{acct}-3"
sub_acct = f"{acct}-4"
other_user = "test-user-177-1"
test_user = atf.properties["test-user"]
slurm_user = atf.properties["slurm-user"]

# SlurmUser and root always see everything.
pytestmark = pytest.mark.skipif(
    atf.properties["test-user-uid"] == 0 or test_user == slurm_user,
    reason="Issue 51259: test-user must not be SlurmUser or root",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config(
        "wants to add accounts and users to the accounting database"
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter("PrivateData", None, source="slurmdbd")
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i add account {acct},{coord_acct},{other_acct}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {sub_acct} parent={coord_acct}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={acct}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {other_user} "
        f"account={acct},{coord_acct},{other_acct},{sub_acct}",
        user=slurm_user,
        fatal=True,
    )

    # The test-user may already be in the database with an AdminLevel.
    admin_level = atf.run_command_output(
        f"sacctmgr -n -P show user {test_user} format=AdminLevel",
        user=slurm_user,
        fatal=True,
    ).strip()
    if admin_level != "None":
        atf.run_command(
            f"sacctmgr -i modify user {test_user} set adminlevel=none",
            user=slurm_user,
            fatal=True,
        )


def users_seen_by(user):
    result = atf.run_command(
        f"sacctmgr -n -P show user {test_user},{other_user} format=User",
        user=user,
    )
    assert result["exit_code"] == 0, f"sacctmgr show user failed for {user}"
    return set(result["stdout"].split())


def assocs_seen_by(user):
    result = atf.run_command(
        "sacctmgr -n -P show assoc "
        f"account={acct},{coord_acct},{other_acct},{sub_acct} format=Account,User",
        user=user,
    )
    assert result["exit_code"] == 0, f"sacctmgr show assoc failed for {user}"
    return {tuple(line.split("|")) for line in result["stdout"].splitlines()}


def test_users():
    """Verify PrivateData=users limits the users and associations a user sees."""

    all_users = {test_user, other_user}
    own_assoc = (acct, test_user)
    peer_assoc = (acct, other_user)
    coord_assoc = (coord_acct, other_user)
    sub_assoc = (sub_acct, other_user)
    other_assoc = (other_acct, other_user)
    all_assocs = {own_assoc, peer_assoc, coord_assoc, sub_assoc, other_assoc}

    assert users_seen_by(test_user) == all_users, "A user should see all users"
    assert all_assocs <= assocs_seen_by(test_user), "A user should see all associations"

    # This restarts Slurm, so slurmdbd starts with PrivateData=users.
    atf.set_config_parameter("PrivateData", "users", source="slurmdbd")

    for user in (slurm_user, "root"):
        assert users_seen_by(user) == all_users, f"{user} should see all users"
        assert all_assocs <= assocs_seen_by(user), f"{user} should see all associations"

    assert users_seen_by(test_user) == {test_user}, "A user should only see themselves"
    assert assocs_seen_by(test_user) == {
        own_assoc
    }, "A user should only see their own association"

    atf.run_command(
        f"sacctmgr -i add coordinator account={coord_acct} names={test_user}",
        user=slurm_user,
        fatal=True,
    )
    assert users_seen_by(test_user) == {
        test_user
    }, "A coordinator should only list themselves"
    assocs = assocs_seen_by(test_user)
    assert (
        coord_assoc in assocs
    ), f"A coordinator should see the associations of {coord_acct}"
    assert (
        sub_assoc in assocs
    ), f"A coordinator should see the associations of {sub_acct}"
    assert (
        peer_assoc not in assocs
    ), f"A coordinator should not see other users in {acct}"
    assert other_acct not in {
        a for a, _ in assocs
    }, f"A coordinator should not see the associations of {other_acct}"

    atf.run_command(
        f"sacctmgr -i modify user {test_user} set adminlevel=operator",
        user=slurm_user,
        fatal=True,
    )
    assert users_seen_by(test_user) == all_users, "An operator should see all users"
    assert all_assocs <= assocs_seen_by(
        test_user
    ), "An operator should see all associations"

    atf.run_command(
        f"sacctmgr -i modify user {test_user} set adminlevel=admin",
        user=slurm_user,
        fatal=True,
    )
    assert users_seen_by(test_user) == all_users, "An admin should see all users"
    assert all_assocs <= assocs_seen_by(
        test_user
    ), "An admin should see all associations"
