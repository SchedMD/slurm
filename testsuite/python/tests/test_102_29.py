############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacctmgr show event names the user that set a node's reason.

slurmdbd stores the name of the user that set a node's reason, and
sacctmgr show event prints that name and filters events by it.
"""

import time

import pytest

import atf

# Unique per run, so events left by an earlier run never match
reason = f"test_102_29 drain {time.time_ns()}"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def drained_node():
    # Drain as root, not SlurmUser, so the event shows the user that asked
    # for the drain rather than the daemon's own user.
    node = next(iter(atf.get_nodes(fatal=True)))
    atf.run_command(
        f"scontrol update nodename={node} state=drain reason='{reason}'",
        user="root",
        fatal=True,
    )
    yield node
    atf.run_command(
        f"scontrol update nodename={node} state=resume",
        user="root",
        fatal=True,
    )


def shown_root():
    """Return how sacctmgr shows root in the User column."""
    if atf.get_version("bin/sacctmgr") < (26, 11):
        # Before 26.11, sacctmgr resolves a stored uid as "name(uid)".
        # Remove this when 26.05 is no longer supported.
        return "root(0)"
    return "root"


def other_user():
    """Return a user that did not drain the node."""
    if atf.properties["slurm-user"] != "root":
        return atf.properties["slurm-user"]
    return atf.get_user_name()


def event_rows(event_filter):
    """Return the (user, reason) of each event that matches the filter."""
    output = atf.run_command_output(
        f"sacctmgr -nP show event {event_filter} format=user,reason",
        fatal=True,
    )
    return [tuple(line.split("|", 1)) for line in output.splitlines() if line]


def drain_users(event_filter):
    """Return the user of each matching event that has our reason."""
    return [
        user
        for user, event_reason in event_rows(event_filter)
        if event_reason == reason
    ]


def wait_for_drain_users(event_filter):
    """slurmdbd records the event asynchronously, so poll for it."""
    users = []
    for _ in atf.timer(fatal=True):
        users = drain_users(event_filter)
        if users:
            break
    return users


def test_event_user(drained_node):
    """The event shows the name of the user that drained the node."""
    users = wait_for_drain_users(f"nodes={drained_node}")

    assert users == [shown_root()], f"Expected the event to be set by root, got {users}"


def test_event_user_filter(drained_node):
    """Filtering events by user lists only that user's events."""
    other = other_user()

    assert wait_for_drain_users(
        "user=root"
    ), "sacctmgr show event user=root should list the drain event"
    users = [user for user, _ in event_rows("user=root")]
    assert all(
        user == shown_root() for user in users
    ), f"sacctmgr show event user=root should only list root's events, got {users}"
    assert not drain_users(
        f"user={other}"
    ), f"Only root drained the node, so user={other} should not list the event"
    assert drain_users(
        f"user=root,{other}"
    ), f"sacctmgr show event user=root,{other} should list the drain event"
