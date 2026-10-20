############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test WCKey handling on job submission (sbatch --wckey)."""

import os

import pytest

import atf

test_user = atf.get_user_name()
test_name = os.path.splitext(os.path.basename(__file__))[0]
test_acct = f"{test_name}_acct"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(True)
    atf.require_config_parameter("TrackWcKey", "yes")
    atf.require_config_parameter("TrackWcKey", "yes", source="slurmdbd")
    atf.require_slurm_running()


def _get_wckeys(user, keylen):
    return map(
        lambda s: s.strip(" "),
        atf.run_command_output(
            f"sacctmgr -n show wckey where user={user} format=wckey%{keylen}",
            user=atf.properties["slurm-user"],
            fatal=True,
        ).split(),
    )


@pytest.fixture(scope="module", autouse=True)
def setup_acct():
    atf.run_command(
        f"sacctmgr -i add account {test_acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del account {test_acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module", autouse=True)
def setup_user():
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={test_acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i del user {test_user}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="function")
def setup_enforce():
    atf.require_config_parameter_includes("AccountingStorageEnforce", "wckeys")


@pytest.fixture(scope="function")
def setup_nonenforce():
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "wckeys")


def test_create_wckey_job_submit(setup_nonenforce):
    test_wckey = f"{test_name}_wckey_create"
    assert test_wckey not in _get_wckeys(
        test_user, len(test_wckey)
    ), f"WCKey {test_wckey} should not exist before the job is submitted"
    job_id = atf.submit_job_sbatch(f"--wckey={test_wckey} --wrap 'sleep 0'", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.wait_for_job_accounted(job_id, "State", "COMPLETED", fatal=True)
    assert test_wckey in _get_wckeys(
        test_user, len(test_wckey)
    ), f"WCKey {test_wckey} should be created when a job using it starts"
    atf.run_command(
        f"sacctmgr -i del user wckey={test_wckey}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )


def test_nocreate_wckey_job_submit(setup_enforce):
    test_wckey = f"{test_name}_wckey_nocreate"
    assert test_wckey not in _get_wckeys(
        test_user, len(test_wckey)
    ), f"WCKey {test_wckey} should not exist before the job is submitted"
    res = atf.run_command(
        f"sbatch --wckey={test_wckey} --wrap 'sleep 0'", xfail=True, fatal=True
    )
    assert (
        "Invalid wckey specification" in res["stderr"]
    ), "sbatch should reject a WCKey the user does not have when enforcing"
    assert test_wckey not in _get_wckeys(
        test_user, len(test_wckey)
    ), f"WCKey {test_wckey} should not be created when enforcing wckeys"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (25, 5),
    reason="Ticket 20771: slurmctld accepted '*' prefixed WCKeys on submission, fixed in 25.05",
)
def test_reject_asterisk_prefix_key(setup_nonenforce):
    test_wckey = f"*{test_name}_wckey_asterisk"
    assert test_wckey not in _get_wckeys(
        test_user, len(test_wckey)
    ), f"WCKey {test_wckey} should not exist before the job is submitted"
    res = atf.run_command(
        f"sbatch --wckey='{test_wckey}' --wrap 'sleep 0'", xfail=True, fatal=True
    )
    assert (
        "Invalid wckey specification" in res["stderr"]
    ), "sbatch should reject a WCKey starting with the reserved '*' prefix"
    assert test_wckey not in _get_wckeys(
        test_user, len(test_wckey)
    ), f"WCKey {test_wckey} should not be created"
