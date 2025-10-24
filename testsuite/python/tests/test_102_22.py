############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacctmgr WCKey default-key logic and removal protection.

Deleting a WCKey with pending or running jobs is refused only when
TrackWCKey is set in slurmdbd.conf (see 23985).
"""

import os

import pytest

import atf

user_name = atf.get_user_name()
test_name = os.path.splitext(os.path.basename(__file__))[0]
wckeys = [f"{test_name}_wckey{idx}" for idx in range(3)]
max_wckey_len = max(map(len, wckeys))
acct = f"{test_name}_acct"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(True)
    atf.require_config_parameter("TrackWcKey", "yes")
    atf.require_config_parameter("TrackWcKey", "yes", source="slurmdbd")
    atf.require_slurm_running()


def _run(cmd, **kwargs):
    return atf.run_command(cmd, user=atf.properties["slurm-user"], fatal=True, **kwargs)


def _assoc_mgr_def_wckey(user):
    output = atf.run_command_output(
        f"scontrol show assoc_mgr users={user} flags=users",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for line in output.splitlines():
        if f"UserName={user}" not in line:
            continue
        for token in line.split():
            key, _, value = token.partition("=")
            if key == "DefWckey":
                return value
    pytest.fail(f"DefWckey not found for user '{user}' in assoc_mgr output")


def _sacct_mgr_def_wckey(user):
    return atf.run_command_output(
        f"sacctmgr -n show user format=defaultwckey%{max_wckey_len} where user={user}",
        user=atf.properties["slurm-user"],
        fatal=True,
    ).rstrip()


@pytest.fixture(scope="module", autouse=True)
def setup_acct():
    # A valid account/association is required for the user to submit jobs.
    _run(f"sacctmgr -i add account {acct}")
    _run(f"sacctmgr -i add user {user_name} account={acct}")
    yield
    _run(f"sacctmgr -i del user {user_name}")
    _run(f"sacctmgr -i del account {acct}")


@pytest.fixture(scope="function", autouse=True)
def setup_wckeys():
    _run(f"sacctmgr -i add user {user_name} wckey={','.join(wckeys)}")
    yield
    _run(f"sacctmgr -i del user {user_name} wckey={','.join(wckeys)}")


def test_first_added_key_is_default():
    """
    when adding wckeys to a user without any, the first key listed should be picked to be the user's default
    """
    user_def_wckey = _sacct_mgr_def_wckey(user_name)
    assert (
        user_def_wckey == wckeys[0]
    ), f"Expected default WCKey {wckeys[0]}, got {user_def_wckey}"


def test_delete_default_clears_assoc_cache():
    """
    when deleting a user's default wckey, the assoc manager in-memory cache should reflect that
    """
    for _ in atf.timer(fatal=True):
        if _assoc_mgr_def_wckey(user_name) == wckeys[0]:
            break
    _run(f"sacctmgr -i delete user wckey={wckeys[0]} where user={user_name}")
    for _ in atf.timer(fatal=True):
        def_wckey = _assoc_mgr_def_wckey(user_name)
        if def_wckey != wckeys[0]:
            break
    assert (
        def_wckey == "(null)"
    ), f"Expected no default WCKey in assoc_mgr after its deletion, got {def_wckey}"


def test_default_preserved_on_new_key():
    """
    when a new key is added, the user's current default shouldn't change

    assumes passing test_first_added_key_is_default()
    """
    newkey = wckeys[0] + "_new"
    user_def_wckey = _sacct_mgr_def_wckey(user_name)
    _run(f"sacctmgr -i add user {user_name} wckey={newkey}")
    assert user_def_wckey == _sacct_mgr_def_wckey(
        user_name
    ), "Default WCKey should not change when a new WCKey is added"
    _run(f"sacctmgr -i del user {user_name} wckey={newkey}")


@pytest.mark.parametrize("hold,exp_job_state", ((False, "RUNNING"), (True, "PENDING")))
def test_no_delete_with_jobs(hold, exp_job_state):
    """
    pending or running jobs shouldn't have their wckey deleted
    """
    test_wckey = wckeys[1]
    hold_arg = "--hold" if hold else ""
    job_id = atf.submit_job_sbatch(
        f"-A {acct} --wckey={test_wckey} {hold_arg} -N 1 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, exp_job_state, fatal=True)
    atf.wait_for_job_accounted(job_id, "WCKey", test_wckey, fatal=True)
    result = _run(
        f"sacctmgr -i delete user wckey={test_wckey} where user={user_name}",
        xfail=True,
    )
    remaining = atf.run_command_output(
        f"sacctmgr -n show wckey where user={user_name} format=wckey%{max_wckey_len}",
        user=atf.properties["slurm-user"],
        fatal=True,
    ).split()
    # Release the job now that the deletion has been attempted, so it no longer
    # holds the wckey when the setup_wckeys fixture tears down.
    atf.cancel_jobs([job_id], fatal=True)
    atf.wait_for_job_accounted(job_id, "State", "CANCELLED", fatal=True)
    assert (
        "Job(s) active" in result["stderr"]
    ), "Deleting a WCKey with an active job should fail with 'Job(s) active'"
    assert (
        test_wckey in remaining
    ), f"WCKey {test_wckey} should still exist after the refused delete"
