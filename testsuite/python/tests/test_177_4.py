############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol show authorization by default, when PrivateData is not set."""

import re

import pytest

import atf

test_account = "acct_show_default_test"
other_account = "acct_show_default_other"
resv_name = "resv_show_default_test"
feature = "feat_show_default_test"

slurm_user = atf.properties["slurm-user"]
test_user = atf.properties["test-user"]

# root and SlurmUser are always Administrators
pytestmark = pytest.mark.skipif(
    test_user in (slurm_user, "root"),
    reason="This test requires a plain SlurmTestUser, not SlurmUser or root",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter("PrivateData", None)
    atf.require_sudo_rights()
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i add account {test_account}", user=slurm_user, fatal=True
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user},{slurm_user} account={test_account}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {other_account}", user=slurm_user, fatal=True
    )
    atf.run_command(
        f"sacctmgr -i add user {slurm_user} account={other_account}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i modify user {test_user} set adminlevel=None",
        user=slurm_user,
        fatal=True,
    )
    # The test user must not be a Coordinator of anything
    coordinators = atf.run_command_output(
        f"sacctmgr -n -P show user {test_user} withcoord format=coordinators",
        user=slurm_user,
        fatal=True,
    )
    for account in (test_account, other_account):
        if account in coordinators:
            atf.run_command(
                f"sacctmgr -i remove coordinator account={account} names={test_user}",
                user=slurm_user,
                fatal=True,
            )


@pytest.fixture(scope="module")
def node(setup):
    node = next(iter(atf.get_nodes(fatal=True)))
    atf.set_node_parameter(node, "Features", feature)
    atf.require_slurm_running()
    return node


def show(command):
    """Return the output of an scontrol show command run as the test user."""
    result = atf.run_command(f"scontrol -o show {command}", user=test_user)
    assert result["exit_code"] == 0, f"The test user should be allowed: {result}"
    return result["stdout"]


def test_show_jobs():
    """Verify a plain user can list another user's job."""
    job_id = atf.submit_job_sbatch(
        f"-A {test_account} -N1 -n1 -t5 -H -o /dev/null -e /dev/null "
        "--wrap 'sleep infinity'",
        user=slurm_user,
        fatal=True,
    )

    assert f"JobId={job_id} " in show("jobs"), f"Job {job_id} should be visible"


@pytest.fixture
def step(setup):
    """A running step of another user's job."""
    job_id = atf.submit_job_sbatch(
        f"-A {test_account} -N1 -n1 -t5 -o /dev/null -e /dev/null "
        "--wrap 'srun sleep infinity'",
        user=slurm_user,
        fatal=True,
    )
    step_id = f"{job_id}.0"
    for _ in atf.timer(fatal=True):
        steps = atf.get_steps(job_id, user=slurm_user, quiet=True)
        if steps.get(step_id, {}).get("State") == "RUNNING":
            break
    return step_id


@pytest.mark.skipif(
    atf.config_parameter_includes(
        "SlurmctldParameters", "enable_stepmgr", live=False, quiet=True
    ),
    reason="Issue 51337: show steps without an argument omits stepmgr jobs",
)
def test_show_steps(step):
    """Verify a plain user can list another user's step."""
    assert f"StepId={step} " in show("steps"), f"Step {step} should be visible"


@pytest.fixture
def reservation(node):
    atf.run_command(
        f"scontrol create reservation={resv_name} starttime=now+1day duration=10 "
        f"nodes={node} users={slurm_user} flags=ignore_jobs",
        user=slurm_user,
        fatal=True,
    )
    yield
    atf.run_command(
        f"scontrol delete reservationname={resv_name}", user=slurm_user, fatal=True
    )


def test_show_reservation(reservation):
    """Verify a plain user can see a reservation it has no Access to."""
    assert f"ReservationName={resv_name} " in show(
        f"reservation {resv_name}"
    ), f"Reservation {resv_name} should be visible"


def test_show_reservations(reservation):
    """Verify a plain user can list a reservation it has no Access to."""
    assert f"ReservationName={resv_name} " in show(
        "reservation"
    ), f"Reservation {resv_name} should be visible"


@pytest.mark.parametrize("command", ["hostnames", "hostlist", "hostlistsorted"])
def test_show_host(node, command):
    """Verify a plain user can expand a hostlist function."""
    result = atf.run_command(
        f"scontrol show {command} 'feature{{{feature}}}'", user=test_user
    )

    assert (
        node in result["stdout"].split()
    ), f"The feature function should expand to {node}"


def test_show_assoc_mgr_users():
    """Verify a plain user can see the users of other users."""
    out = show("assoc_mgr flags=users")

    users = set(re.findall(r"UserName=(\S+)\(", out))
    assert {test_user, slurm_user} <= users, f"All users should be visible: {users}"


def test_show_assoc_mgr_assoc():
    """Verify a plain user can see the associations of other users."""
    out = show("assoc_mgr flags=assoc")
    seen = {
        (account, user)
        for account, user in re.findall(
            r"Account=(\S+) UserName=(\S*?)(?:\(\d+\))? ", out
        )
    }

    expected = {
        (test_account, ""),
        (test_account, test_user),
        (test_account, slurm_user),
        (other_account, ""),
        (other_account, slurm_user),
    }
    assert expected <= seen, f"All associations should be visible: {expected - seen}"
