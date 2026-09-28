############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_102_21.py - limits inherited from a parent account follow a move.

An association that does not set a limit itself takes its parent's, and the
database hands the controller that resolved value rather than an empty
column. Moving the association therefore changes what it enforces without
changing any of its own columns, and the controller has to be told.

The move has to be made after the controller has already been given the old
parent's value, or there is no stale value to get wrong: a child created
while the controller is running arrives with the column unset, and the
limit is reached by walking to the parent instead. Restarting between the
two is what puts the resolved value in the controller's record.
"""

import re

import pytest

import atf

test_parent1 = "test_par1_102_21"
test_parent2 = "test_par2_102_21"
test_child = "test_child_102_21"

# Every limit below is set to these on the two parents, the first being the
# tighter, so moving the child from the first to the second is always a move to
# a looser parent.
TIGHT = 5
LOOSE = 9

OWN_MAX_JOBS = 3

# Every limit slurmdbd inherits on a move. DefaultQOS is inherited as well but
# scontrol show assoc_mgr never prints it, so it cannot be observed here.
#
# scontrol prints a count limit as "Name=value(used)" and the rest bare, so
# each entry carries which shape to read.
#
# (sacctmgr keyword, scontrol label, printed with a (used) suffix)
INHERITED_LIMITS = [
    ("MaxJobs", "MaxJobs", True),
    ("MaxJobsAccrue", "MaxJobsAccrue", True),
    ("MaxSubmitJobs", "MaxSubmitJobs", True),
    ("MaxWall", "MaxWallPJ", False),
    ("MinPrioThresh", "MinPrioThresh", False),
    ("Priority", "Priority", False),
]
INHERITED_IDS = [kw for kw, _, _ in INHERITED_LIMITS]

# The subset a coordinator move is also checked against, so every limit that
# can trigger the refusal the coordinator case below is about. Grp* limits are
# checked but never inherited - get_parent_limits does not return them - and
# Priority is inherited but not checked, so neither can reach it.
GUARDED_LIMITS = [entry for entry in INHERITED_LIMITS if entry[0] != "Priority"]
LIMIT_IDS = [kw for kw, _, _ in GUARDED_LIMITS]

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("modifies accounts on the configured cluster")
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def accounts():
    """Two parents with different MaxJobs, and a child under the first."""
    su = atf.properties["slurm-user"]
    cluster = atf.get_config_parameter("ClusterName")
    atf.run_command(f"sacctmgr -i add user {su} account=root", user=su)
    for acct in (test_parent1, test_parent2):
        atf.run_command(
            f"sacctmgr -i add account {acct} cluster={cluster}",
            user=su,
            fatal=True,
        )
    atf.run_command(
        f"sacctmgr -i add account {test_child} cluster={cluster} "
        f"parent={test_parent1}",
        user=su,
        fatal=True,
    )
    yield cluster
    for acct in (test_child, test_parent1, test_parent2):
        atf.run_command(f"sacctmgr -i delete account {acct}", user=su)


def child_limit(label, has_used):
    """The limit the controller holds for the child, or None when unset.

    Anchored on the label so MaxJobs is not read off MaxJobsAccrue, and the
    (used) suffix is required only for the limits that print one - demanding it
    of MaxWallPJ would never match and would read as unset.
    """
    out = atf.run_command_output(
        f"scontrol show assoc_mgr accounts={test_child} flags=assoc",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    pattern = rf"\b{label}=(\d*)" + (r"\(" if has_used else "")
    match = re.search(pattern, out)
    if not match or not match.group(1):
        return None
    return int(match.group(1))


def child_max_jobs():
    """MaxJobs the controller holds for the child, or None when unset."""
    return child_limit("MaxJobs", True)


def child_parent_account():
    """The parent account the controller holds for the child, or None."""
    out = atf.run_command_output(
        f"scontrol show assoc_mgr accounts={test_child} flags=assoc",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    match = re.search(r"\bParentAccount=(\S+?)\(", out)
    if not match:
        return None
    return match.group(1)


def wait_for_child_limit(label, has_used, want):
    for _ in atf.timer():
        if child_limit(label, has_used) == want:
            return True
    return False


def set_parent_limits(keyword):
    """Give the two parents only this limit, tighter on the first.

    One limit at a time on purpose. With every limit set at once, a move to the
    looser parent raises all of them, so a coordinator refusal about any single
    one would fail every case and name none of them.
    """
    for acct, value in ((test_parent1, TIGHT), (test_parent2, LOOSE)):
        atf.run_command(
            f"sacctmgr -i modify account {acct} set {keyword}={value}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


def move_child_to(parent):
    atf.run_command(
        f"sacctmgr -i modify account {test_child} set parent={parent}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Ticket 23787: slurmdbd only sends limits inherited from a new "
    "parent on an account move from 26.11",
)
@pytest.mark.parametrize("keyword,label,has_used", INHERITED_LIMITS, ids=INHERITED_IDS)
def test_moved_account_inherits_new_parent_limit(keyword, label, has_used):
    """A limit the child does not set itself follows it to the new parent.

    Run for every inherited limit rather than MaxJobs alone: each goes through
    its own _inherits() call naming its own request field, own column and
    parent column, so a slip in any one of them is only visible here.
    DefaultQOS is inherited too, but scontrol show assoc_mgr does not print it.
    """
    set_parent_limits(keyword)
    # Restart so the controller is handed the limit resolved against the
    # first parent. Without this the column is simply unset and there is
    # nothing stale for the move to leave behind.
    atf.restart_slurmctld()
    assert child_limit(label, has_used) == TIGHT, (
        f"Setup: controller should hold the first parent's {keyword} as the "
        f"child's own after a restart"
    )

    move_child_to(test_parent2)

    assert wait_for_child_limit(label, has_used, LOOSE), (
        f"Controller still holds {child_limit(label, has_used)} after the "
        f"move; it should enforce the new parent's {keyword} of {LOOSE}"
    )


def test_moved_account_keeps_its_own_limit():
    """A limit the child sets itself is not replaced by the new parent's.

    The expected MaxJobs is the value held before the move, so the move has to
    be observed landing first. Read too early the assertion sees the pre-move
    record and holds for a reason that has nothing to do with the limit.
    """
    set_parent_limits("MaxJobs")
    atf.run_command(
        f"sacctmgr -i modify account {test_child} set maxjobs={OWN_MAX_JOBS}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.restart_slurmctld()
    assert child_max_jobs() == OWN_MAX_JOBS, "Setup: own MaxJobs applied"

    move_child_to(test_parent2)

    for _ in atf.timer(fatal=True):
        if child_parent_account() == test_parent2:
            break

    assert (
        child_max_jobs() == OWN_MAX_JOBS
    ), "The account's own MaxJobs should survive the move"


@pytest.fixture(scope="function")
def coordinator():
    """Make the test user a coordinator of both parents.

    Coordinator status is inherited down the tree, so coordinating the first
    parent covers the child being moved, and the move itself is only permitted
    to someone who coordinates the destination as well.
    """
    su = atf.properties["slurm-user"]
    user = atf.properties["test-user"]
    # slurmdbd treats root and SlurmUser as admins, and an admin never takes
    # the coordinator branch, so the cases below would pass without reaching
    # what they guard. test-user falls back to the invoking user when
    # slurmtestuser is unset in testsuite.conf.
    assert user not in (su, "root"), (
        f"test-user resolves to {user!r}, which slurmdbd treats as an admin; "
        f"set slurmtestuser in testsuite.conf to an unprivileged account"
    )
    added = False

    # 'add coordinator' needs the user to exist in accounting. Add only what is
    # missing and remove exactly that, since a site-owned test user has to
    # survive the run.
    if not atf.run_command_output(
        f"sacctmgr -n -P show user {user} format=User", user=su
    ).strip():
        added = True
        atf.run_command(
            f"sacctmgr -i add user {user} account={test_parent1}",
            user=su,
            fatal=True,
        )
    atf.run_command(
        f"sacctmgr -i add coordinator account={test_parent1},{test_parent2} "
        f"names={user}",
        user=su,
        fatal=True,
    )

    yield user

    atf.run_command(
        f"sacctmgr -i remove coordinator "
        f"account={test_parent1},{test_parent2} names={user}",
        user=su,
    )
    if added:
        atf.run_command(f"sacctmgr -i remove user {user}", user=su)


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Ticket 23787: slurmdbd only sends limits inherited from a new "
    "parent on an account move from 26.11",
)
@pytest.mark.parametrize("keyword,label,has_used", GUARDED_LIMITS, ids=LIMIT_IDS)
def test_coordinator_may_move_to_a_looser_parent(coordinator, keyword, label, has_used):
    """A coordinator may move an account to a parent with a higher limit.

    The move makes the child inherit the new parent's MaxJobs, which is looser
    than the one it had from the old parent, and the controller's cached record
    still holds the old one. That must not be read as the coordinator raising a
    limit: the request asks for no limit at all, and a coordinator of both
    parents is allowed to move an account between them either way.

    Without the inherited values being kept away from the coordinator check,
    this fails with "Coordinators can not increase MaxJobs above the parent
    limit" and the whole modify is aborted.
    """
    set_parent_limits(keyword)
    atf.restart_slurmctld()
    assert child_limit(label, has_used) == TIGHT, (
        f"Setup: controller should hold the first parent's {keyword} as the "
        f"child's own after a restart"
    )

    result = atf.run_command(
        f"sacctmgr -i modify account {test_child} set parent={test_parent2}",
        user=coordinator,
    )
    assert result["exit_code"] == 0, (
        f"a coordinator of both parents should be able to move an account to "
        f"the looser one, but got: {result['stderr'].strip()!r}"
    )

    # A command that returned 0 without doing anything would otherwise pass.
    assert wait_for_child_limit(label, has_used, LOOSE), (
        f"the move reported success but the controller still holds "
        f"{child_limit(label, has_used)}, not the new parent's {keyword} "
        f"of {LOOSE}"
    )
