############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol show authorization when PrivateData is set."""

import re

import pytest

import atf

test_account = "acct_show_auth_test"
other_account = "acct_show_auth_other"
resv_access = "resv_show_auth_access"
resv_other = "resv_show_auth_other"
feature = "feat_show_auth_test"

slurm_user = atf.properties["slurm-user"]
test_user = atf.properties["test-user"]

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
#
# A test leaves out the actors whose case test_177_2 already checks.
actors = ["owner", "user", "coordinator", "operator", "admin", "slurm-user", "root"]
cluster_actors = [a for a in actors if a != "owner"]
scoped_actors = actors + ["coordinator-other"]

admin = ["admin", "slurm-user", "root"]
privileged = admin + ["operator"]
effective_owner = ["owner", "coordinator"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    for value in ["jobs", "nodes", "partitions", "reservations", "usage", "users"]:
        atf.require_config_parameter_includes("PrivateData", value)
    atf.require_sudo_rights()
    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def setup_db():
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
    # PrivateData=nodes denies the test user the node list
    node = next(iter(atf.get_nodes(user=slurm_user, fatal=True)))
    atf.set_node_parameter(node, "Features", feature)
    atf.require_slurm_running()
    return node


@pytest.fixture(scope="module")
def partition(setup):
    # PrivateData=partitions denies the test user the partition list
    partitions = atf.get_partitions(user=slurm_user, fatal=True)
    return next(name for name, p in partitions.items() if p.get("Default") == "YES")


@pytest.fixture(scope="module")
def reservations(node):
    """A reservation the test user may use, and one it may not."""
    for name, user, start in [
        (resv_access, test_user, "now+1day"),
        (resv_other, slurm_user, "now+2day"),
    ]:
        atf.run_command(
            f"scontrol create reservation={name} starttime={start} duration=10 "
            f"nodes={node} users={user} flags=ignore_jobs",
            user=slurm_user,
            fatal=True,
        )
    yield
    for name in [resv_access, resv_other]:
        atf.run_command(
            f"scontrol delete reservationname={name}", user=slurm_user, fatal=True
        )


def submit_sbatch(owner, args, account=test_account):
    return atf.submit_job_sbatch(
        f"-A {account} -N1 -n1 -t5 -o /dev/null -e /dev/null {args}",
        user=owner,
        fatal=True,
    )


def probe_coordinator(expected, account=test_account):
    """Wait until the test user is, or is not, a Coordinator of account."""
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

    # Who runs the scontrol command, and who owns the job it acts on
    if name == "slurm-user":
        return {"name": name, "user": slurm_user, "owner": test_user}
    if name == "root":
        return {"name": name, "user": "root", "owner": test_user}
    if name == "owner":
        return {"name": name, "user": test_user, "owner": test_user}
    return {"name": name, "user": test_user, "owner": slurm_user}


def who(actor):
    """Describe the actor."""
    return f"{actor['name']} (user {actor['user']})"


def assert_visible(actor, visible, expected, what):
    assert (
        visible == expected
    ), f"{what} should {'' if expected else 'not '}be visible to {who(actor)}"


def show(actor, command, denied=None):
    """Return the output of an scontrol show command run as the actor.

    PrivateData hides jobs, steps and reservations from the output, and a
    command for a single hidden one fails as if it were missing. It denies the
    other commands outright: if denied is given, the command must fail with an
    authorization error reported by that call.
    """
    result = atf.run_command(f"scontrol -o show {command}", user=actor["user"])
    if denied is not None:
        assert (
            result["exit_code"] != 0
        ), f"scontrol show {command} should not be allowed for {who(actor)}"
        assert f"{denied}: Access/permission denied" in result["stderr"], (
            f"scontrol show {command} by {who(actor)} should be denied by an "
            f"authorization check in {denied}: {result['stderr']}"
        )
    return result["stdout"]


@pytest.mark.parametrize("actor", ["admin", "slurm-user"], indirect=True)
def test_show_job(actor):
    """Verify only Privileged users or Effective Owners can see a job."""
    job_id = submit_sbatch(actor["owner"], "-H --wrap 'sleep infinity'")

    out = show(actor, f"job {job_id}")

    assert_visible(
        actor,
        f"JobId={job_id} " in out,
        actor["name"] in privileged + effective_owner,
        f"Job {job_id}",
    )


@pytest.mark.parametrize(
    "actor",
    ["coordinator", "operator", "admin", "slurm-user", "root", "coordinator-other"],
    indirect=True,
)
def test_show_jobs(actor):
    """Verify only Privileged users or Effective Owners can list a job."""
    jobs = {
        user: submit_sbatch(user, "-H --wrap 'sleep infinity'")
        for user in (test_user, slurm_user)
    }

    out = show(actor, "jobs")

    for user, job_id in jobs.items():
        assert_visible(
            actor,
            f"JobId={job_id} " in out,
            user == actor["user"] or actor["name"] in privileged + ["coordinator"],
            f"Job {job_id} of {user}",
        )


def submit_step(owner):
    """Submit a job as owner and return the step it runs, once it is running."""
    job_id = submit_sbatch(owner, "--wrap 'srun sleep infinity'")
    step_id = f"{job_id}.0"
    for _ in atf.timer(fatal=True):
        steps = atf.get_steps(job_id, user=slurm_user, quiet=True)
        if steps.get(step_id, {}).get("State") == "RUNNING":
            break
    return step_id


@pytest.mark.parametrize(
    "actor",
    ["owner", "coordinator", "operator", "admin", "slurm-user", "coordinator-other"],
    indirect=True,
)
def test_show_step(actor):
    """Verify only Privileged users or Effective Owners can see a step."""
    step_id = submit_step(actor["owner"])

    out = show(actor, f"step {step_id}")
    visible = f"StepId={step_id} " in out

    if (
        not visible
        and atf.config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
        and actor["name"] in ["coordinator", "operator", "admin"]
    ):
        pytest.xfail(
            "Issue 51331: slurmd ignores Coordinators and AdminLevel for "
            "show step of stepmgr jobs under PrivateData=jobs"
        )

    assert_visible(
        actor,
        visible,
        actor["name"] in privileged + effective_owner,
        f"Step {step_id}",
    )


@pytest.mark.skipif(
    atf.config_parameter_includes(
        "SlurmctldParameters", "enable_stepmgr", live=False, quiet=True
    ),
    reason="Issue 51337: show steps without an argument omits stepmgr jobs",
)
@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
def test_show_steps(actor):
    """Verify only Privileged users or Effective Owners can list a step."""
    step_id = submit_step(actor["owner"])

    out = show(actor, "steps")

    assert_visible(
        actor,
        f"StepId={step_id} " in out,
        actor["name"] in privileged + effective_owner,
        f"Step {step_id}",
    )


@pytest.mark.parametrize("actor", ["coordinator", "admin"], indirect=True)
def test_show_node(actor, node):
    """Verify only Privileged users can see a node."""
    allowed = actor["name"] in privileged

    # PrivateData=partitions also fails show node, so check which call failed
    out = show(actor, f"node {node}", None if allowed else "slurm_load_node error")

    assert_visible(actor, f"NodeName={node} " in out, allowed, f"Node {node}")


@pytest.mark.parametrize("actor", ["coordinator", "admin"], indirect=True)
def test_show_partition(actor, partition):
    """Verify only Privileged users can see a partition."""
    allowed = actor["name"] in privileged

    out = show(
        actor,
        f"partition {partition}",
        None if allowed else "slurm_load_partitions error",
    )

    assert_visible(
        actor, f"PartitionName={partition} " in out, allowed, f"Partition {partition}"
    )


@pytest.mark.parametrize("actor", cluster_actors, indirect=True)
@pytest.mark.parametrize("access", [True, False])
def test_show_reservation(actor, reservations, access):
    """Verify only Privileged users or users with Access can see a reservation."""
    name = resv_access if access else resv_other
    out = show(actor, f"reservation {name}")

    assert_visible(
        actor,
        f"ReservationName={name} " in out,
        access or actor["name"] in privileged,
        f"Reservation {name}",
    )


@pytest.mark.parametrize("actor", cluster_actors, indirect=True)
def test_show_reservations(actor, reservations):
    """Verify only Privileged users or users with Access can list a reservation."""
    out = show(actor, "reservation")

    for name, access in [(resv_access, True), (resv_other, False)]:
        assert_visible(
            actor,
            f"ReservationName={name} " in out,
            access or actor["name"] in privileged,
            f"Reservation {name}",
        )


@pytest.mark.parametrize("actor", cluster_actors, indirect=True)
@pytest.mark.parametrize("command", ["hostnames", "hostlist", "hostlistsorted"])
def test_show_host(actor, node, command):
    """Verify only Privileged users can expand a hostlist function.

    Host names and ranges never need authorization.
    """
    allowed = actor["name"] in privileged

    out = show_host(actor, command, node, True)
    assert_visible(actor, node in out.split(), True, f"The expansion of {node}")

    out = show_host(actor, command, f"'feature{{{feature}}}'", allowed)
    assert_visible(
        actor,
        node in out.split(),
        allowed,
        f"The expansion of feature{{{feature}}}",
    )


def show_host(actor, command, hostlist, allowed):
    """Return the output of an scontrol show host* command run as the actor.

    slurmctld expands a hostlist function, so a denied one must report that it
    failed to expand.
    """
    result = atf.run_command(f"scontrol show {command} {hostlist}", user=actor["user"])
    failed = "failed to expand hostlist function" in result["stderr"]
    assert failed != allowed, (
        f"The expansion of {hostlist} by {who(actor)} should "
        f"{'' if allowed else 'not '}be allowed: {result['stderr']}"
    )
    return result["stdout"]


@pytest.mark.parametrize("actor", cluster_actors, indirect=True)
def test_show_assoc_mgr_users(actor):
    """Verify only Privileged users, not Coordinators, can see other users."""
    out = show(actor, "assoc_mgr flags=users")
    users = set(re.findall(r"UserName=(\S+)\(", out))

    assert_visible(actor, test_user in users, True, f"User {test_user}")
    assert_visible(
        actor,
        slurm_user in users,
        actor["name"] in privileged or actor["user"] == slurm_user,
        f"User {slurm_user}",
    )


@pytest.mark.parametrize("actor", cluster_actors + ["coordinator-other"], indirect=True)
def test_show_assoc_mgr_assoc(actor):
    """Verify only Privileged users can see all associations."""
    everything = {
        (test_account, ""),
        (test_account, test_user),
        (test_account, slurm_user),
        (other_account, ""),
        (other_account, slurm_user),
    }
    if actor["name"] in privileged:
        expected = everything
    else:
        expected = {(test_account, test_user)}
        if actor["name"] == "coordinator":
            expected |= {a for a in everything if a[0] == test_account}
        elif actor["name"] == "coordinator-other":
            expected |= {a for a in everything if a[0] == other_account}

    out = show(actor, "assoc_mgr flags=assoc")
    seen = {
        (account, user)
        for account, user in re.findall(
            r"Account=(\S+) UserName=(\S*?)(?:\(\d+\))? ", out
        )
        if account in (test_account, other_account)
    }

    assert seen == expected, f"{who(actor)} should see the associations {expected}"
