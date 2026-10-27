############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol authorization for job, step, partition and reservation commands.

Claims are checked against every authorization level: a plain user acting on its
own job (direct owner) or on another user's job, an account coordinator of the
job's account or another account, AdminLevel=Operator, AdminLevel=Administrator,
SlurmUser and root. An allowed request must succeed and take effect, and a
denied one must fail without taking effect.
"""

import re

import pytest

import atf

test_account = "acct_auth_test"
other_account = "acct_auth_other"
part_name = "part_auth_test"
resv_name = "resv_auth_test"
script_marker = "marker_auth_test"

slurm_user = atf.properties["slurm-user"]
test_user = atf.properties["test-user"]

denied_re = re.compile(r"Access/permission denied|Invalid user id")

# The authorization levels the test user is given, and whose job it acts on.
#   owner:             plain user acting on its own job
#   user:              plain user acting on SlurmUser's job
#   coordinator:       Coordinator of the job's account
#   coordinator-other: Coordinator of an unrelated account, acting on
#                      SlurmUser's job
#   operator:          AdminLevel=Operator
#   admin:             AdminLevel=Administrator
#   slurm-user:        SlurmUser acting on the test user's job
#   root:              root acting on the test user's job
actors = ["owner", "user", "coordinator", "operator", "admin", "slurm-user", "root"]
cluster_actors = [a for a in actors if a != "owner"]
# Actors for tests that allow Coordinators, plus a coordinator of an unrelated
# account, which must be denied because the rule is scoped to the job's account
scoped_actors = actors + ["coordinator-other"]

admin = ["admin", "slurm-user", "root"]
privileged = admin + ["operator"]
effective_owner = ["owner", "coordinator"]

suspend_actors = [
    pytest.param(
        "owner",
        marks=pytest.mark.xfail(
            (26, 5) <= atf.get_version("sbin/slurmctld") < (26, 5, 6),
            reason="Issue 51327: Since 26.05.0 owners could suspend their jobs",
        ),
    ),
] + [name for name in scoped_actors if name != "owner"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_excludes("PreemptMode", "GANG")
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
    atf.run_command(
        f"sacctmgr -i add account {other_account}", user=slurm_user, fatal=True
    )
    atf.run_command(
        f"sacctmgr -i add user {slurm_user} account={other_account}",
        user=slurm_user,
        fatal=True,
    )


@pytest.fixture(scope="module")
def node(setup):
    return next(iter(atf.get_nodes(fatal=True)))


def submit_sbatch(owner, args, account=test_account):
    return atf.submit_job_sbatch(
        f"-A {account} -N1 -n1 -t5 -o /dev/null -e /dev/null {args}",
        user=owner,
        fatal=True,
    )


def probe_coordinator(expected, account=test_account):
    """Wait until slurmctld agrees whether the test user coordinates account.

    Holding another user's job is allowed to coordinators and denied to plain
    users.
    """
    job_id = submit_sbatch(
        slurm_user, "--begin=now+1day --wrap 'sleep infinity'", account
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

    coord_account = None
    if name == "coordinator":
        coord_account = test_account
    elif name == "coordinator-other":
        coord_account = other_account
    if coord_account:
        atf.run_command(
            f"sacctmgr -i add coordinator account={coord_account} names={test_user}",
            user=slurm_user,
            fatal=True,
        )
    coordinators = atf.run_command_output(
        f"sacctmgr -n -P show user {test_user} withcoord format=coordinators",
        user=slurm_user,
        fatal=True,
    )
    for account in (test_account, other_account):
        if account != coord_account and account in coordinators:
            atf.run_command(
                f"sacctmgr -i remove coordinator account={account} names={test_user}",
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
    if name == "coordinator-other":
        probe_coordinator(True, other_account)

    # "user" runs the scontrol command, and "owner" submits the job it acts on
    if name == "slurm-user":
        return {"name": name, "user": slurm_user, "owner": test_user}
    if name == "root":
        return {"name": name, "user": "root", "owner": test_user}
    if name == "owner":
        return {"name": name, "user": test_user, "owner": test_user}
    return {"name": name, "user": test_user, "owner": slurm_user}


# State is read as SlurmUser, so PrivateData can't hide the jobs and steps of
# another user from the test user.
def job_param(job_id, name):
    return atf.get_job_parameter(job_id, name, user=slurm_user, fatal=True)


def step_param(step_id, name):
    steps = atf.get_steps(step_id, user=slurm_user)
    return steps.get(step_id, {}).get(name)


def who(actor):
    """Describe the actor."""
    return f"{actor['name']} (user {actor['user']})"


def assert_access(actor, result, allowed, took_effect=None):
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
    if took_effect is not None:
        assert took_effect == allowed, (
            f"The request by {who(actor)} should "
            f"{'' if allowed else 'not '}have taken effect"
        )


def running_job(actor, args="--wrap 'sleep infinity'"):
    job_id = submit_sbatch(actor["owner"], args)
    atf.wait_for_job_state(job_id, "RUNNING", user=slurm_user, fatal=True)
    return job_id


@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
def test_update_job(actor):
    """Verify only Privileged users or Effective Owners can update a job."""
    job_id = submit_sbatch(actor["owner"], "-H --wrap 'sleep infinity'")
    result = atf.run_command(
        f"scontrol update jobid={job_id} comment={script_marker}", user=actor["user"]
    )

    assert_access(
        actor,
        result,
        actor["name"] in privileged + effective_owner,
        job_param(job_id, "Comment") == script_marker,
    )


@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
def test_requeue(actor):
    """Verify only Privileged users or Effective Owners can requeue a job."""
    job_id = running_job(actor, "--requeue --wrap 'sleep infinity'")
    result = atf.run_command(f"scontrol requeue {job_id}", user=actor["user"])

    assert_access(
        actor,
        result,
        actor["name"] in privileged + effective_owner,
        job_param(job_id, "Restarts") == 1,
    )


@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
def test_update_step(actor):
    """Verify only Privileged users or Effective Owners can update a step."""
    job_id = running_job(actor, "--wrap 'srun sleep infinity'")
    step_id = f"{job_id}.0"
    for _ in atf.timer(fatal=True):
        if step_param(step_id, "State") == "RUNNING":
            break
    result = atf.run_command(
        f"scontrol update stepid={step_id} timelimit=3", user=actor["user"]
    )

    assert_access(
        actor,
        result,
        actor["name"] in privileged + effective_owner,
        step_param(step_id, "TimeLimit") == "00:03:00",
    )


@pytest.mark.parametrize("actor", suspend_actors, indirect=True)
def test_suspend_resume(actor):
    """Verify only Privileged users or Coordinators can suspend and resume a job."""
    allowed = actor["name"] in privileged + ["coordinator"]
    job_id = running_job(actor)

    result = atf.run_command(f"scontrol suspend {job_id}", user=actor["user"])
    assert_access(actor, result, allowed, job_param(job_id, "JobState") == "SUSPENDED")

    # The resume needs a suspended job even if the actor couldn't suspend it
    if not allowed:
        atf.run_command(f"scontrol suspend {job_id}", user=slurm_user, fatal=True)
        atf.wait_for_job_state(job_id, "SUSPENDED", user=slurm_user, fatal=True)
    result = atf.run_command(f"scontrol resume {job_id}", user=actor["user"])
    assert_access(actor, result, allowed, job_param(job_id, "JobState") == "RUNNING")


@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
@pytest.mark.parametrize("command", ["hold", "uhold"])
def test_hold(actor, command):
    """Verify only Privileged users or Effective Owners can hold a job."""
    allowed = actor["name"] in privileged + effective_owner
    job_id = submit_sbatch(actor["owner"], "--begin=now+1day --wrap 'sleep infinity'")

    result = atf.run_command(f"scontrol {command} {job_id}", user=actor["user"])
    assert_access(actor, result, allowed, job_param(job_id, "Priority") == 0)

    # Only a hold by a Privileged user is an admin-hold
    if allowed:
        admin_hold = command == "hold" and actor["name"] in privileged
        reason = "JobHeldAdmin" if admin_hold else "JobHeldUser"
        assert (
            job_param(job_id, "Reason") == reason
        ), f"{command} by {who(actor)} should place a {reason} hold"


@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
@pytest.mark.parametrize("hold", ["uhold", "hold"])
def test_release(actor, hold):
    """Verify only Privileged users or Effective Owners can release a job."""
    job_id = submit_sbatch(actor["owner"], "--begin=now+1day --wrap 'sleep infinity'")
    atf.run_command(f"scontrol {hold} {job_id}", user=slurm_user, fatal=True)

    # Only Privileged users can release an admin-hold
    allowed_names = privileged + (effective_owner if hold == "uhold" else [])
    allowed = actor["name"] in allowed_names
    result = atf.run_command(f"scontrol release {job_id}", user=actor["user"])
    assert_access(actor, result, allowed, job_param(job_id, "Priority") != 0)


def test_notify(actor):
    """Verify only root, SlurmUser or the direct owner can notify a job."""
    job_id = running_job(actor)
    result = atf.run_command(
        f"scontrol notify {job_id} {script_marker}", user=actor["user"]
    )

    # The message goes to the job, there is nothing for the test to observe
    assert_access(actor, result, actor["name"] in ["owner", "slurm-user", "root"])


def test_write_batch_script(actor):
    """Verify only Privileged users or the direct owner can get a batch script."""
    job_id = submit_sbatch(
        actor["owner"], f"-H --wrap 'sleep infinity # {script_marker}'"
    )
    result = atf.run_command(
        f"scontrol write batch_script {job_id} -", user=actor["user"]
    )

    # scontrol exits 0 when the script retrieval fails, so the exit code isn't
    # checked
    if actor["name"] in privileged + ["owner"]:
        assert (
            script_marker in result["stdout"]
        ), f"{who(actor)} should get the batch script: {result['stderr']}"
    else:
        assert denied_re.search(
            result["stderr"]
        ), f"{who(actor)} should be denied by an authorization check"
        assert (
            script_marker not in result["stdout"]
        ), f"{who(actor)} should not get the batch script"


@pytest.fixture
def cleanup():
    yield
    atf.run_command(f"scontrol delete reservationname={resv_name}", user=slurm_user)
    atf.run_command(f"scontrol delete partitionname={part_name}", user=slurm_user)


def show(kind, name):
    """Return the one-line description of an entity, or None if it is missing."""
    result = atf.run_command(f"scontrol -F -o show {kind} {name}", user=slurm_user)
    if result["exit_code"] != 0:
        return None
    return result["stdout"]


def create_update_delete(actor, allowed, kind, name, create, update):
    """Run a create, update and delete of an entity as the actor."""
    result = atf.run_command(f"scontrol create {create}", user=actor["user"])
    assert_access(actor, result, allowed, show(kind, name) is not None)

    # Create the entity so that only authorization rejects an update or delete
    if not allowed:
        atf.run_command(f"scontrol create {create}", user=slurm_user, fatal=True)

    result = atf.run_command(
        f"scontrol update {kind}name={name} {update}", user=actor["user"]
    )
    updated = re.search(rf"\b{update}\b", show(kind, name), re.IGNORECASE)
    assert_access(actor, result, allowed, updated is not None)

    result = atf.run_command(f"scontrol delete {kind}name={name}", user=actor["user"])
    assert_access(actor, result, allowed, show(kind, name) is None)


@pytest.mark.parametrize("actor", cluster_actors, indirect=True)
def test_partition(actor, node, cleanup):
    """Verify only Administrators can create, update and delete a partition."""
    create_update_delete(
        actor,
        actor["name"] in admin,
        "partition",
        part_name,
        f"partitionname={part_name} nodes={node} state=down",
        "maxtime=00:07:00",
    )


@pytest.mark.parametrize("actor", cluster_actors, indirect=True)
def test_reservation(actor, node, cleanup):
    """Verify only Privileged users can create, update and delete a reservation."""
    create_update_delete(
        actor,
        actor["name"] in privileged,
        "reservation",
        resv_name,
        f"reservation={resv_name} starttime=now+1day duration=10 nodes={node} "
        f"users={slurm_user} flags=ignore_jobs",
        "duration=00:20:00",
    )


# Only denials are checked, and only by the reply: a reconfigure leaves
# nothing to observe, and a shutdown stops slurmctld asynchronously.
@pytest.mark.parametrize(
    "actor", [a for a in cluster_actors if a not in admin], indirect=True
)
@pytest.mark.parametrize(
    "command",
    [
        pytest.param(
            "update hresname=hres_auth_test layername=layer count=1",
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 11),
                reason="Issue 50445: HRES was added in 26.11",
            ),
        ),
        "reconfigure",
        "shutdown",
    ],
)
def test_admin_only(actor, command):
    """Verify only Administrators can update an hres, reconfigure or shut down."""
    result = atf.run_command(f"scontrol {command}", user=actor["user"])
    assert_access(actor, result, False)
