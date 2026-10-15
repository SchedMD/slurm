############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify the HTTP status slurmrestd returns for a request slurmdbd rejects.

A slurmdbd error reaches slurmrestd differently than a slurmctld one. It is
still a client error, so it must come back as 422 rather than 500, carrying
the error that names why.
"""

import os

import pytest
import requests

import atf

# Any data_parser works: the status code is chosen before the response is
# serialized. Pinned to the newest so it survives version removals longest.
data_parser = "v0.0.46"

test_name = os.path.splitext(os.path.basename(__file__))[0]
account_name = f"{test_name}_acct"
other_account_name = f"{test_name}_acct2"
user_name = atf.get_user_name()


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmrestd",
        reason="Ticket 25423: data_parser v0.0.46 was added in 26.11",
    )
    atf.require_slurmrestd("slurmctld,slurmdbd,util", data_parser)
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt", source="slurmdbd")
    # slurmdbd only refuses to remove a default account when a user is required
    # to have one.
    atf.require_config_parameter("AllowNoDefAcct", "No", source="slurmdbd")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def admin_level(default_account):
    """Grants the test user the accounting rights an account removal needs."""
    slurm_user = atf.properties["slurm-user"]
    current = atf.run_command_output(
        f"sacctmgr -n show user {user_name} format=AdminLevel",
        user=slurm_user,
        fatal=True,
    ).strip()

    if current.startswith("Admin"):
        yield
        return

    atf.run_command(
        f"sacctmgr -i modify user {user_name} set adminlevel=Admin",
        user=slurm_user,
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i modify user {user_name} set adminlevel={current or 'None'}",
        user=slurm_user,
        fatal=True,
    )


@pytest.fixture(scope="module")
def default_account(setup):
    """Makes an account the test user's default, which blocks its removal.

    slurmdbd only protects a default account when the user keeps another one.
    """
    slurm_user = atf.properties["slurm-user"]
    # An absent user prints nothing, a present one prints its name and its
    # default account. Which of the two it is decides how much teardown has
    # to undo, since a user added here has to go away with the accounts.
    previous = atf.run_command_output(
        f"sacctmgr -n show user {user_name} format=User,DefaultAccount",
        user=slurm_user,
        fatal=True,
    ).split()
    user_existed = bool(previous)
    previous_default = previous[1] if len(previous) > 1 else ""

    atf.run_command(
        f"sacctmgr -i add account {account_name},{other_account_name}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user_name} "
        f"account={account_name},{other_account_name}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i modify user {user_name} set defaultaccount={account_name}",
        user=slurm_user,
        fatal=True,
    )

    yield account_name

    # The accounts cannot be removed while one of them is still the user's
    # default, so the user has to stop pointing at them first. A user that was
    # already in the database gets its own default back; one added here has no
    # default to go back to, so it goes away instead, which is how it was
    # found. AllowNoDefAcct=No leaves no third case.
    if previous_default:
        atf.run_command(
            f"sacctmgr -i modify user {user_name} "
            f"set defaultaccount={previous_default}",
            user=slurm_user,
            fatal=True,
        )
    elif not user_existed:
        atf.run_command(
            f"sacctmgr -i remove user {user_name}",
            user=slurm_user,
            fatal=True,
        )
    atf.run_command(
        f"sacctmgr -i remove account {account_name},{other_account_name}",
        user=slurm_user,
        fatal=True,
    )


def test_default_account_removal_returns_422(default_account, admin_level):
    """Verify an account the slurmdbd refuses to remove answers 422."""
    r = requests.delete(
        f"{atf.properties['slurmrestd_url']}/slurmdb/{data_parser}/account/{default_account}",
        headers=atf.properties["slurmrestd-headers"],
        timeout=30,
    )
    assert r.status_code == 422, f"removal answered {r.status_code}: {r.text}"
    body = r.json()
    assert len(body["errors"]) == 1, f"removal should report one error: {r.text}"
    assert body["errors"][0]["error"], f"removal should describe its error: {r.text}"
    assert not body["warnings"], f"removal should warn about nothing: {r.text}"
