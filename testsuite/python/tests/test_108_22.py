############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol authorization for node commands.

Claims are checked against every authorization level that applies to the
cluster: a plain user, an account coordinator, AdminLevel=Operator,
AdminLevel=Administrator, SlurmUser and root. An allowed request must succeed
and take effect, and a denied one must fail without taking effect. These are
kept apart from test_108_21 because creating a node needs MaxNodeCount, which
needs select/cons_tres.
"""

import re

import pytest

import atf

test_account = "acct_node_auth_test"
dyn_node = "dyn_auth_test"

slurm_user = atf.properties["slurm-user"]
test_user = atf.properties["test-user"]

denied_re = re.compile(r"Access/permission denied|Invalid user id")

# The authorization levels the test user is given.
#   user:        plain user
#   coordinator: Coordinator of an account
#   operator:    AdminLevel=Operator
#   admin:       AdminLevel=Administrator
#   slurm-user:  SlurmUser
#   root:        root
actors = ["user", "coordinator", "operator", "admin", "slurm-user", "root"]

admin = ["admin", "slurm-user", "root"]
privileged = admin + ["operator"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter("MaxNodeCount", 64)
    # Required for MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_sudo_rights()
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i add account {test_account}", user=slurm_user, fatal=True
    )
    atf.run_command(
        f"sacctmgr -i add user {','.join({test_user, slurm_user})} account={test_account}",
        user=slurm_user,
        fatal=True,
    )


def probe_coordinator(expected):
    """Wait until slurmctld agrees whether the test user coordinates the account.

    Holding another user's job is allowed to coordinators and denied to plain
    users.
    """
    job_id = atf.submit_job_sbatch(
        f"-A {test_account} -N1 -n1 -t5 -o /dev/null -e /dev/null "
        "--begin=now+1day --wrap 'sleep infinity'",
        user=slurm_user,
        fatal=True,
    )

    for _ in atf.timer(fatal=True):
        rc = atf.run_command_exit(f"scontrol hold {job_id}", user=test_user, quiet=True)
        if (rc == 0) == expected:
            break
    atf.cancel_jobs([job_id], user=slurm_user, fatal=True)


@pytest.fixture(scope="module", params=actors)
def actor(request, setup):
    """Give the test user the authorization level of this actor."""
    name = request.param

    # SlurmUser and root are always Administrators. If the test user is one of
    # them, only actors that don't run as the test user check their own claims.
    if test_user in [slurm_user, "root"] and name not in ["slurm-user", "root"]:
        pytest.skip("This test requires a plain SlurmTestUser, not SlurmUser or root")

    level = "None"
    if name == "operator":
        level = "Operator"
    elif name == "admin":
        level = "Administrator"
    atf.run_command(
        f"sacctmgr -i modify user {test_user} set adminlevel={level}",
        user=slurm_user,
        fatal=True,
    )

    coordinators = atf.run_command_output(
        f"sacctmgr -n -P show user {test_user} withcoord format=coordinators",
        user=slurm_user,
        fatal=True,
    )
    if name == "coordinator":
        atf.run_command(
            f"sacctmgr -i add coordinator account={test_account} names={test_user}",
            user=slurm_user,
            fatal=True,
        )
    elif test_account in coordinators:
        atf.run_command(
            f"sacctmgr -i remove coordinator account={test_account} names={test_user}",
            user=slurm_user,
            fatal=True,
        )

    for _ in atf.timer(fatal=True):
        out = atf.run_command_output(
            f"scontrol show assoc_mgr flags=users users={test_user}",
            user=slurm_user,
            quiet=True,
            fatal=True,
        )
        if re.search(rf"AdminLevel={level}\b", out):
            break
    if name not in privileged:
        probe_coordinator(name == "coordinator")

    if name == "slurm-user":
        return {"name": name, "user": slurm_user}
    if name == "root":
        return {"name": name, "user": "root"}
    return {"name": name, "user": test_user}


def who(actor):
    """Describe the actor."""
    return f"{actor['name']} (user {actor['user']})"


def assert_access(actor, result, allowed, took_effect):
    """Check that the request was allowed or denied, and whether it took effect."""
    if allowed:
        assert (
            result["exit_code"] == 0
        ), f"{who(actor)} should be allowed: {result['stderr']}"
    else:
        assert (
            result["exit_code"] != 0
        ), f"{who(actor)} should not be allowed: {result['command']}"
        assert denied_re.search(result["stderr"]), (
            f"{who(actor)} should be denied by an authorization check: "
            f"{result['stderr']}"
        )
    assert took_effect == allowed, (
        f"The request by {who(actor)} should "
        f"{'' if allowed else 'not '}have taken effect"
    )


@pytest.fixture
def cleanup():
    yield
    atf.run_command(f"scontrol delete nodename={dyn_node}", user=slurm_user)


def show_node():
    """Return the one-line description of the node, or None if it is missing."""
    result = atf.run_command(f"scontrol -F -o show node {dyn_node}", user=slurm_user)
    if result["exit_code"] != 0:
        return None
    return result["stdout"]


def test_node(actor, cleanup):
    """Verify only Administrators can create, update and delete a node."""
    allowed = actor["name"] in admin
    create = f"scontrol create nodename={dyn_node} state=future"

    result = atf.run_command(create, user=actor["user"])
    assert_access(actor, result, allowed, show_node() is not None)

    # Create the entity so that only authorization rejects an update or delete
    if not allowed:
        atf.run_command(create, user=slurm_user, fatal=True)

    result = atf.run_command(
        f"scontrol update nodename={dyn_node} weight=7", user=actor["user"]
    )
    updated = re.search(r"\bweight=7\b", show_node(), re.IGNORECASE)
    assert_access(actor, result, allowed, updated is not None)

    result = atf.run_command(f"scontrol delete nodename={dyn_node}", user=actor["user"])
    assert_access(actor, result, allowed, show_node() is None)
