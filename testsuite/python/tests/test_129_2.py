############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test user name case handling with the PreserveCaseUser option."""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Global variables

cluster = "test_cluster"
user_lcase = "test_user"
user_ucase = user_lcase.upper()
acct = "test_acct"
acct2 = "test_acct2"
wckey = "test_wckey"

assoc_table = f"{cluster}_assoc_table"
user_table = "user_table"


@pytest.fixture(scope="module", autouse=True)
def setup(sql_statement_repeat):
    """Test setup with required configurations."""
    atf.require_version(
        (26, 11),
        component="sbin/slurmdbd",
        reason="Ticket 23675: user name case not preserved when toggling PreserveCaseUser",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("AllowNoDefAcct", "No", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "yes", source="slurmdbd")
    atf.require_config_parameter_excludes(
        "Parameters", "PreserveCaseUser", source="slurmdbd"
    )
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i add cluster {cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.run_command(
        f"sacctmgr -i add account {acct} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.run_command(
        f"sacctmgr -i add account {acct2} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.run_command(
        f"sacctmgr -i add user {user_lcase} defaultaccount={acct} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    #
    # Age user creation time by a week so user will be marked as deleted and
    # not actually removed from the user table later.
    # Alternatively, we could submit a job as the user to accomplish this but
    # there would need to be a user created on the system with the matching
    # case. Aging is easier.
    #
    atf.stop_slurmdbd(quiet=True)
    mysql_command = sql_statement_repeat + ' -e "'
    mysql_command += f"update {user_table} set creation_time=creation_time-7*24*3600 where name='{user_lcase}'; "
    mysql_command += f"update {assoc_table} set creation_time=creation_time-7*24*3600 where user='{user_lcase}'"
    mysql_command += '"'
    atf.run_command(
        mysql_command,
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.start_slurmdbd(quiet=True)

    # Leave the aged user soft-deleted with PCU unset: the same baseline
    # each test's teardown restores, so every test starts identically.
    _reset_db_baseline()


def _reset_db_baseline():
    """Return the db to baseline: PreserveCaseUser unset and the aged test
    user (and its associations) soft-deleted."""
    atf.remove_config_parameter_value(
        "Parameters", "PreserveCaseUser", source="slurmdbd"
    )
    atf.run_command(
        f"sacctmgr -i remove user {user_lcase} cluster={cluster}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove assoc where user={user_lcase} cluster={cluster}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture(scope="function")
def setup_db(request):
    """Set up the db for a test: PreserveCaseUser set with the requested
    user present, starting from the baseline established by setup(). Tears
    down to that baseline afterwards, so each test is independent and can
    be run in isolation."""

    user = request.param

    # Set PreserveCaseUser and add the requested user
    atf.add_config_parameter_value("Parameters", "PreserveCaseUser", source="slurmdbd")
    atf.run_command(
        f"sacctmgr -i add user {user} defaultaccount={acct} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Ensure the case of the user and assoc rows match
    atf.run_command(
        f"sacctmgr -i modify user {user} cluster={cluster} set newname={user}",
        user=atf.properties["slurm-user"],
    )

    # Start test with PCU set and user created
    yield

    # Restore baseline so the next test starts clean
    _reset_db_baseline()


@pytest.mark.parametrize("setup_db", [user_lcase], indirect=True)
def test_remove_and_add_user_no_pcu_to_pcu(setup_db):
    """Test removing and adding user with different case when
    transitioning from PCU not being set to it being set
    """

    # Lower case user was created with PCU not set
    # Now removing and adding upper case user with PCU set

    # Remove the lower case user
    atf.run_command(
        f"sacctmgr -i remove user {user_lcase} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # The removed row must remain soft-deleted (deleted=1), not purged,
    # otherwise the differing-case collision this test exercises never occurs.
    deleted_user = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} withdeleted format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        deleted_user
    ), f"Removed user '{user_lcase}' should remain soft-deleted to exercise the case collision"

    # Now add the upper case user
    atf.run_command(
        f"sacctmgr -i add user {user_ucase} acct={acct} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_ucase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert user_ut, f"User '{user_ucase}' does not exist"

    user_at = atf.run_command_output(
        f"sacctmgr -nP show assoc where user={user_ucase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_ucase
    ), f"User table user '{user_ut}' should preserve original case '{user_ucase}'"
    assert (
        user_at == user_ucase
    ), f"Assoc table user '{user_at}' should preserve original case '{user_ucase}'"


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_remove_and_add_user_pcu_to_no_pcu(setup_db):
    """Test removing and adding user with different case when
    transitioning from PCU being set to it not being set
    """

    # Upper case user was created with PCU set
    # Now removing and adding lower case user with PCU not set

    # Turn off PCU, remove existing user and add new one
    atf.remove_config_parameter_value(
        "Parameters", "PreserveCaseUser", source="slurmdbd"
    )
    atf.run_command(
        f"sacctmgr -i remove user {user_lcase} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # The removed row must remain soft-deleted (deleted=1), not purged,
    # otherwise the differing-case collision this test exercises never occurs.
    deleted_user = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} withdeleted format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        deleted_user
    ), f"Removed user '{user_lcase}' should remain soft-deleted to exercise the case collision"

    atf.run_command(
        f"sacctmgr -i add user {user_lcase} acct={acct} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Check that the user case in the user and assoc tables match
    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert user_ut, f"User '{user_lcase}' does not exist"

    user_at = atf.run_command_output(
        f"sacctmgr -nP show assoc where user={user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_lcase
    ), f"User table user '{user_ut}' should be lowercased to '{user_lcase}'"
    assert (
        user_at == user_lcase
    ), f"Assoc table user '{user_at}' should be lowercased to '{user_lcase}'"


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_modify_user_reports_original_case(setup_db):
    """Test modifying a user with different case reports the original case."""

    output = atf.run_command_output(
        f"sacctmgr -i modify user {user_lcase} cluster={cluster} set adminlevel=operator",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        " Modified users...\n" in output
    ), f"sacctmgr modify did not report 'Modified users':\n{output}"
    assert re.search(
        rf"^\s+{user_ucase}$", output, re.MULTILINE
    ), f"Modify user should list '{user_ucase}' instead of normalizing to '{user_lcase}'"

    # The stored user must still hold the original case after the modify
    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_ucase
    ), f"Stored user '{user_ut}' should preserve original case '{user_ucase}'"


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_remove_user_reports_original_case(setup_db):
    """Test removing a user with different case reports the original case."""

    output = atf.run_command_output(
        f"sacctmgr -i remove user {user_lcase}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        " Deleting users" in output
    ), f"sacctmgr remove did not report 'Deleting users':\n{output}"
    assert re.search(
        rf"^\s+{user_ucase}$", output, re.MULTILINE
    ), f"Remove user should list '{user_ucase}' instead of normalizing to '{user_lcase}'"


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_add_assoc_different_case_preserves_user(setup_db):
    """Test adding an assoc for an existing user with a different case
    reuses the existing user (original case) instead of creating a new
    assoc with the differing case."""

    # user_ucase already exists (acct) under PCU. Add an assoc for the
    # same user but with a differing case (user_lcase) and a different
    # account (acct2). The new assoc must use the original case.
    atf.run_command(
        f"sacctmgr -i add user {user_lcase} acct={acct2} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # The user table should still hold only the original-case user
    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_ucase
    ), f"User table user '{user_ut}' should preserve original case '{user_ucase}'"

    # The new assoc on acct2 should be created under the original case
    user_at = atf.run_command_output(
        f"sacctmgr -nP show assoc where user={user_lcase} account={acct2} cluster={cluster} format=user",
        fatal=True,
    ).rstrip()
    assert user_at == user_ucase, (
        f"New assoc user '{user_at}' should preserve original case '{user_ucase}' "
        f"instead of using the differing case '{user_lcase}'"
    )


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_add_assoc_different_case_with_default_preserves_user(setup_db):
    """Test adding an assoc for an existing user with a different case and an
    explicit default account still reuses the existing user (original case)
    instead of creating an assoc with the differing case."""

    # user_ucase already exists (acct) under PCU. Add an assoc for the same
    # user with a differing case (user_lcase), a different account (acct2),
    # and an explicit default account so case preservation runs even when the
    # default-account branch is taken.
    atf.run_command(
        f"sacctmgr -i add user {user_lcase} acct={acct2} defaultaccount={acct2} "
        f"cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # The user table should still hold only the original-case user
    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_ucase
    ), f"User table user '{user_ut}' should preserve original case '{user_ucase}'"

    # The new assoc on acct2 should be created under the original case
    user_at = atf.run_command_output(
        f"sacctmgr -nP show assoc where user={user_lcase} account={acct2} cluster={cluster} format=user",
        fatal=True,
    ).rstrip()
    assert user_at == user_ucase, (
        f"New assoc user '{user_at}' should preserve original case '{user_ucase}' "
        f"instead of using the differing case '{user_lcase}'"
    )


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_modify_user_by_default_account_preserves_case(setup_db):
    """Test modifying users selected by default account preserves the
    original user case."""

    # user_ucase already exists with default account acct under PCU.
    # Select it by default account rather than by name; the modify must
    # still resolve to the user and preserve its stored case.
    output = atf.run_command_output(
        f"sacctmgr -i modify user where defaultaccount={acct} cluster={cluster} set adminlevel=operator",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        " Modified users...\n" in output
    ), f"sacctmgr modify did not report 'Modified users':\n{output}"
    assert re.search(
        rf"^\s+{user_ucase}$", output, re.MULTILINE
    ), f"Modify by default account should list '{user_ucase}' instead of normalizing to '{user_lcase}'"

    # The stored user must still hold the original case after the modify
    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_ucase
    ), f"Stored user '{user_ut}' should preserve original case '{user_ucase}'"


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_modify_user_by_default_wckey_preserves_case(setup_db):
    """Test modifying users selected by default wckey preserves the
    original user case."""

    # Give user_ucase a default wckey (the first wckey added becomes the
    # default) so it can be selected by default wckey below.
    atf.run_command(
        f"sacctmgr -i add user {user_ucase} wckey={wckey} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Select the user by default wckey rather than by name; the modify
    # must still resolve to the user and preserve its stored case.
    output = atf.run_command_output(
        f"sacctmgr -i modify user where defaultwckey={wckey} cluster={cluster} set adminlevel=operator",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert (
        " Modified users...\n" in output
    ), f"sacctmgr modify did not report 'Modified users':\n{output}"
    assert re.search(
        rf"^\s+{user_ucase}$", output, re.MULTILINE
    ), f"Modify by default wckey should list '{user_ucase}' instead of normalizing to '{user_lcase}'"

    # The stored user must still hold the original case after the modify
    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_ucase
    ), f"Stored user '{user_ut}' should preserve original case '{user_ucase}'"


@pytest.mark.parametrize("setup_db", [user_ucase], indirect=True)
def test_add_user_no_pcu_forces_lowercase(setup_db):
    """Test that without PreserveCaseUser a mixed-case user name is forced
    to lowercase in both the user and assoc tables."""

    # Turn off PCU, then add a mixed-case user
    atf.remove_config_parameter_value(
        "Parameters", "PreserveCaseUser", source="slurmdbd"
    )
    atf.run_command(
        f"sacctmgr -i remove user {user_lcase} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user_ucase} acct={acct} cluster={cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # The stored name must be lowercased in both tables
    user_ut = atf.run_command_output(
        f"sacctmgr -nP show user {user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_ut == user_lcase
    ), f"Without PreserveCaseUser, user '{user_ut}' should be lowercased to '{user_lcase}'"

    user_at = atf.run_command_output(
        f"sacctmgr -nP show assoc where user={user_lcase} format=user cluster={cluster}",
        fatal=True,
    ).rstrip()
    assert (
        user_at == user_lcase
    ), f"Without PreserveCaseUser, assoc user '{user_at}' should be lowercased to '{user_lcase}'"
