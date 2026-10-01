############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmrestd query flags whose names contain spaces."""

import pytest
import requests

import atf

acct_name = "test-acct-112-4"
acct2_name = f"{acct_name}-2"
user_name = "test-user-112-4"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5, 5),
        "sbin/slurmrestd",
        reason="Ticket 25876: query flags with spaces were fixed in 26.05.5",
    )
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt", source="slurmdbd")
    atf.require_slurmrestd("slurmdbd", "v0.0.45")
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i add account {acct_name},{acct2_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user_name} account={acct_name},{acct2_name} "
        f"defaultaccount={acct_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i delete user {user_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i delete account {acct_name},{acct2_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _user_accounts(params):
    response = requests.get(
        f"{atf.properties['slurmrestd_url']}/slurmdb/v0.0.45/associations",
        headers=atf.properties["slurmrestd-headers"],
        params={"user": user_name, **params},
    )
    assert (
        response.status_code == 200
    ), f"Query should return HTTP 200, got {response.status_code}"
    resp = response.json()
    assert not resp["errors"], f"Unexpected errors: {resp['errors']}"
    return sorted(assoc["account"] for assoc in resp["associations"])


def test_flag_with_spaces():
    """Verify a query flag whose name contains spaces is applied"""

    assert _user_accounts({}) == sorted(
        [acct_name, acct2_name]
    ), f"User {user_name} should have associations with both accounts"

    assert _user_accounts({"Filter to only defaults": "true"}) == [
        acct_name
    ], f"Only the default account {acct_name} should be returned with the flag"
