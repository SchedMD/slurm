############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test PrivateData in slurm.conf."""

import re

import pytest

import atf

acct = "test-acct-177-2"
coord_acct = f"{acct}-2"
other_acct = f"{acct}-3"
test_user = atf.properties["test-user"]
slurm_user = atf.properties["slurm-user"]
admins = (slurm_user, "root")

# SlurmUser and root always see everything.
pytestmark = pytest.mark.skipif(
    atf.properties["test-user-uid"] == 0 or test_user == slurm_user,
    reason="Issue 50564: test-user must not be SlurmUser or root",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter("PrivateData", None)
    atf.require_config_parameter("PriorityType", "priority/multifactor")
    atf.require_sudo_rights()
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i add account {acct},{coord_acct},{other_acct}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user},{slurm_user} account={acct},{coord_acct}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {slurm_user} account={other_acct}",
        user=slurm_user,
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        if shares_seen_by(slurm_user) == {
            test_user,
            slurm_user,
        } and slurm_user in assoc_users_seen_by(slurm_user, other_acct):
            break

    # The test-user may already be in the database with an AdminLevel.
    set_admin_level("None")


@pytest.fixture(autouse=True)
def reset_private_data():
    yield
    atf.set_config_parameter("PrivateData", None)


@pytest.fixture
def reset_admin_level():
    yield
    set_admin_level("None")


@pytest.fixture
def reset_coordinator():
    yield
    atf.run_command(
        f"sacctmgr -i delete coordinator account={coord_acct} names={test_user}",
        user=slurm_user,
    )


@pytest.fixture
def jobs():
    blocker = atf.submit_job_sbatch(
        '--exclusive -o /dev/null --wrap "sleep infinity"', user=slurm_user, fatal=True
    )
    atf.wait_for_job_state(blocker, "RUNNING", user=slurm_user, fatal=True)
    node = atf.get_job_parameter(blocker, "NodeList", user=slurm_user, fatal=True)

    # Pending and not held, so sprio lists them.
    own_job = atf.submit_job_sbatch(
        f'-w {node} -o /dev/null --wrap "sleep infinity"', fatal=True
    )
    other_job = atf.submit_job_sbatch(
        f'-w {node} -A {acct} -o /dev/null --wrap "sleep infinity"',
        user=slurm_user,
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        if {own_job, other_job} <= prio_jobs_seen_by(
            slurm_user
        ) and f"{blocker}.batch" in steps_seen_by(slurm_user, blocker):
            break
    return own_job, other_job, blocker


def set_private_data(value):
    atf.set_config_parameter("PrivateData", value)
    for flag in value.split(","):
        assert atf.config_parameter_includes(
            "PrivateData", flag
        ), f"PrivateData={flag} is not active"


def set_admin_level(level):
    current = atf.run_command_output(
        f"sacctmgr -n -P show user {test_user} format=AdminLevel",
        user=slurm_user,
        fatal=True,
    ).strip()
    if current != level:
        atf.run_command(
            f"sacctmgr -i modify user {test_user} set adminlevel={level}",
            user=slurm_user,
            fatal=True,
        )
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr users={test_user} flags=users",
            user=slurm_user,
            fatal=True,
        )
        if f"AdminLevel={level}" in output:
            break


def jobs_seen_by(user, options=""):
    result = atf.run_command(f"squeue -h -o %i {options}", user=user)
    assert result["exit_code"] == 0, f"squeue {options} failed for {user}"
    return {int(job_id) for job_id in result["stdout"].split()}


def prio_jobs_seen_by(user):
    result = atf.run_command("sprio -h -o %i", user=user)
    assert result["exit_code"] == 0, f"sprio failed for {user}"
    return {int(job_id) for job_id in result["stdout"].split()}


def shares_seen_by(user):
    result = atf.run_command(f"sshare -a -n -P -A {acct} -o User", user=user)
    assert result["exit_code"] == 0, f"sshare failed for {user}"
    return set(result["stdout"].split())


def steps_seen_by(user, job_id):
    result = atf.run_command("squeue -h -s -o %i", user=user)
    assert result["exit_code"] == 0, f"squeue -s failed for {user}"
    return {s for s in result["stdout"].split() if s.startswith(f"{job_id}.")}


def assoc_users_seen_by(user, account):
    result = atf.run_command(
        f"scontrol show assoc_mgr flags=assoc accounts={account}", user=user
    )
    assert result["exit_code"] == 0, f"scontrol show assoc_mgr failed for {user}"
    return set(re.findall(r"\bUserName=([^(\s]+)\(", result["stdout"]))


def show(user, entity, name=""):
    """Return the exit code and the names in scontrol show output."""
    key = {
        "job": "JobId",
        "step": "StepId",
        "node": "NodeName",
        "partition": "PartitionName",
    }[entity]
    result = atf.run_command(f"scontrol -a -o show {entity} {name}", user=user)
    names = set(re.findall(rf"\b{key}=(\S+)", result["stdout"]))
    return result["exit_code"], names


def sinfo_names(user, field):
    return set(atf.run_command_output(f"sinfo -a -h -N -o {field}", user=user).split())


def test_jobs(jobs):
    """Verify PrivateData=jobs hides other users' jobs and steps."""

    own_job, other_job, blocker = jobs
    all_jobs = {own_job, other_job}
    step = f"{blocker}.batch"
    options = ["", "-a", f"--jobs={own_job},{other_job}"]
    single_user = f"-u {slurm_user}"
    for option in options:
        assert all_jobs <= jobs_seen_by(
            test_user, option
        ), f"squeue {option} should show all jobs by default"
    assert other_job in jobs_seen_by(
        test_user, single_user
    ), "squeue -u should show other users' jobs by default"
    assert show(test_user, "job", other_job) == (
        0,
        {str(other_job)},
    ), "scontrol should show other users' jobs by default"
    assert step in steps_seen_by(
        test_user, blocker
    ), "squeue -s should show other users' steps by default"
    assert show(test_user, "step", step)[1] == {
        step
    }, "scontrol should show other users' steps by default"

    set_private_data("jobs")

    for option in options:
        seen = jobs_seen_by(test_user, option)
        assert own_job in seen, f"squeue {option} should show the user's own job"
        assert other_job not in seen, f"squeue {option} should hide other users' jobs"
    assert own_job in jobs_seen_by(
        test_user, f"-u {test_user}"
    ), "squeue -u should show the user's own job"
    assert other_job not in jobs_seen_by(
        test_user, single_user
    ), "squeue -u should hide other users' jobs"

    rc, seen = show(test_user, "job")
    assert rc == 0, "scontrol show job should work for a user"
    assert str(own_job) in seen, "scontrol should show the user's own job"
    assert str(other_job) not in seen, "scontrol should hide other users' jobs"
    assert show(test_user, "job", own_job) == (
        0,
        {str(own_job)},
    ), "scontrol should show the user's own job by id"
    rc, seen = show(test_user, "job", other_job)
    assert rc != 0, "scontrol show job should fail for another user's job"
    assert not seen, "scontrol should not print another user's job"
    assert not steps_seen_by(
        test_user, blocker
    ), "squeue -s should hide other users' steps"
    assert not show(test_user, "step", step)[
        1
    ], "scontrol should not print another user's step"

    for user in admins:
        for option in options:
            assert all_jobs <= jobs_seen_by(
                user, option
            ), f"{user} should see all jobs with squeue {option}"
        assert other_job in jobs_seen_by(
            user, single_user
        ), f"{user} should see other users' jobs with squeue -u"
        assert show(user, "job", other_job) == (
            0,
            {str(other_job)},
        ), f"{user} should see other users' jobs with scontrol"
        assert step in steps_seen_by(
            user, blocker
        ), f"{user} should see other users' steps with squeue -s"
        assert show(user, "step", step)[1] == {
            step
        }, f"{user} should see other users' steps with scontrol"


@pytest.mark.skip(
    reason="Issue 51376: sprio from a user aborts slurmctld with PrivateData=jobs"
)
def test_sprio(jobs, reset_admin_level):
    """Verify PrivateData=jobs hides other users' jobs from sprio."""

    own_job, other_job, _ = jobs
    all_jobs = {own_job, other_job}
    assert all_jobs <= prio_jobs_seen_by(
        test_user
    ), "sprio should show all pending jobs by default"

    set_private_data("jobs")

    seen = prio_jobs_seen_by(test_user)
    assert own_job in seen, "sprio should show the user's own job"
    assert other_job not in seen, "sprio should hide other users' jobs"
    for user in admins:
        assert all_jobs <= prio_jobs_seen_by(user), f"{user} should see all jobs"

    set_admin_level("Operator")

    assert all_jobs <= prio_jobs_seen_by(
        test_user
    ), "An Operator should see all jobs with sprio"


def test_coordinator(jobs, reset_coordinator):
    """Verify a coordinator sees the jobs of its account with PrivateData=jobs."""

    _, other_job, _ = jobs
    coord_job = atf.submit_job_sbatch(
        f'--begin=now+1day -A {coord_acct} -o /dev/null --wrap "sleep infinity"',
        user=slurm_user,
        fatal=True,
    )

    set_private_data("jobs")

    assert show(test_user, "job", coord_job)[0] != 0, "A user should not see the job"

    atf.run_command(
        f"sacctmgr -i add coordinator account={coord_acct} names={test_user}",
        user=slurm_user,
        fatal=True,
    )
    # A coordinator may hold the jobs of its account.
    for _ in atf.timer(fatal=True):
        if atf.run_command_exit(f"scontrol hold {coord_job}", user=test_user) == 0:
            break

    assert show(test_user, "job", coord_job) == (
        0,
        {str(coord_job)},
    ), "A coordinator should see the jobs of its account"
    assert (
        show(test_user, "job", other_job)[0] != 0
    ), "A coordinator should not see the jobs of other accounts"


def test_usage():
    """Verify PrivateData=usage hides other users' usage from sshare and scontrol."""

    all_users = {test_user, slurm_user}
    assert (
        shares_seen_by(test_user) == all_users
    ), "sshare should show all usage by default"
    assert slurm_user in assoc_users_seen_by(
        test_user, other_acct
    ), "scontrol should show all associations by default"

    set_private_data("usage")

    assert shares_seen_by(test_user) == {
        test_user
    }, "sshare should only show the user's own usage"
    assert test_user in assoc_users_seen_by(
        test_user, acct
    ), "scontrol should show the user's own association"
    assert not assoc_users_seen_by(
        test_user, other_acct
    ), "scontrol should hide the associations of other accounts"
    for user in admins:
        assert shares_seen_by(user) == all_users, f"{user} should see all usage"
        assert slurm_user in assoc_users_seen_by(
            user, other_acct
        ), f"{user} should see all associations"


@pytest.mark.parametrize(
    "entity,field", [("node", "%N"), ("partition", "%R")], ids=["nodes", "partitions"]
)
def test_cluster_info(entity, field):
    """Verify PrivateData hides nodes and partitions from scontrol and sinfo."""

    rc, names = show(slurm_user, entity)
    assert rc == 0 and names, f"SlurmUser should see the {entity}s"
    listed = sinfo_names(slurm_user, field)
    assert listed, f"SlurmUser should see the {entity}s with sinfo"
    assert show(test_user, entity) == (
        0,
        names,
    ), f"scontrol should show all {entity}s by default"
    assert (
        sinfo_names(test_user, field) == listed
    ), f"sinfo should show all {entity}s by default"

    set_private_data(f"{entity}s")

    rc, seen = show(test_user, entity)
    assert rc != 0, f"scontrol show {entity} should fail for a user"
    assert not seen, f"scontrol should not print {entity}s to a user"
    assert listed.isdisjoint(
        sinfo_names(test_user, field)
    ), f"sinfo should not show {entity}s to a user"
    for user in admins:
        assert show(user, entity) == (0, names), f"{user} should see all {entity}s"
        assert (
            sinfo_names(user, field) == listed
        ), f"{user} should see all {entity}s with sinfo"


def test_operator(jobs, reset_admin_level):
    """Verify an Operator sees the data hidden by PrivateData."""

    own_job, other_job, _ = jobs
    all_jobs = {own_job, other_job}
    _, nodes = show(slurm_user, "node")
    _, partitions = show(slurm_user, "partition")
    listed_nodes = sinfo_names(slurm_user, "%N")
    listed_partitions = sinfo_names(slurm_user, "%R")
    assert nodes and partitions, "SlurmUser should see nodes and partitions"
    assert listed_nodes and listed_partitions, "SlurmUser should see sinfo data"

    set_private_data("jobs,nodes,partitions,usage")

    assert (all_jobs & jobs_seen_by(test_user)) == {
        own_job
    }, "squeue should only show the user's own job"
    assert show(test_user, "job", other_job)[0] != 0, "scontrol should hide the job"
    assert shares_seen_by(test_user) == {test_user}, "sshare should hide usage"
    assert not assoc_users_seen_by(
        test_user, other_acct
    ), "scontrol should hide associations"
    assert show(test_user, "node")[0] != 0, "scontrol should hide nodes"
    assert show(test_user, "partition")[0] != 0, "scontrol should hide partitions"
    assert listed_nodes.isdisjoint(
        sinfo_names(test_user, "%N")
    ), "sinfo should hide nodes"
    assert listed_partitions.isdisjoint(
        sinfo_names(test_user, "%R")
    ), "sinfo should hide partitions"

    set_admin_level("Operator")

    assert all_jobs <= jobs_seen_by(test_user), "An Operator should see all jobs"
    assert show(test_user, "job", other_job) == (
        0,
        {str(other_job)},
    ), "An Operator should see other users' jobs with scontrol"
    assert shares_seen_by(test_user) == {
        test_user,
        slurm_user,
    }, "An Operator should see all usage"
    assert slurm_user in assoc_users_seen_by(
        test_user, other_acct
    ), "An Operator should see all associations"
    assert show(test_user, "node") == (0, nodes), "An Operator should see all nodes"
    assert show(test_user, "partition") == (
        0,
        partitions,
    ), "An Operator should see all partitions"
    assert (
        sinfo_names(test_user, "%N") == listed_nodes
    ), "An Operator should see nodes in sinfo"
    assert (
        sinfo_names(test_user, "%R") == listed_partitions
    ), "An Operator should see partitions in sinfo"
