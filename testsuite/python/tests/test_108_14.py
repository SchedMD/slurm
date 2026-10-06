############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test node event accounting around instance updates and reason storage."""

import shlex
import time

import pytest

import atf

# A node's accounting event is written asynchronously, so the checks below poll
# rather than read once.
DRAIN_REASON = "evt_instupd"
BARRIER_REASON = "evt_barrier"

# tinytext stores 255 bytes; a reason this long used to be truncated, so later
# node_down comparisons never matched the row and every update reopened it.
LONG_REASON = "evt_long_" + ("x" * 280)

OUT_OF_SERVICE_STATES = ("DOWN", "DRAIN", "FAIL")


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes(setup):
    """Returns two nodes that can be taken in and out of service.

    A node leaves FUTURE only on an explicit resume or a registration, so
    waiting for one to leave it cannot make progress. Filter the FUTURE ones
    out instead, and say which nodes were found if there are not two left.
    """

    names = [
        node
        for node in atf.get_nodes(fatal=True)
        if "FUTURE" not in str(atf.get_node_parameter(node, "state") or "").upper()
    ][:2]
    assert len(names) == 2, f"test needs 2 non-FUTURE nodes, found {names!r}"
    return names


@pytest.fixture(autouse=True)
def in_service(nodes):
    """Runs the test against nodes in service, and leaves them that way.

    Resuming first closes anything an earlier test left open, so the event this
    test opens is the only one it has to reason about.
    """

    _return_to_service(nodes)

    # A drain only books an event once the node has no running or completing
    # job: update_node() gates its clusteracct_storage_g_node_down() call on
    # run_job_cnt and comp_job_cnt both being zero. Job cancellation at the end
    # of an earlier file is asynchronous, so a node can still be COMPLETING
    # here, and every _wait_open() below would then time out as if accounting
    # were broken. Fail on the precondition instead.
    atf.wait_for_node_state(nodes, "IDLE", fatal=True)

    yield
    _return_to_service(nodes)


def _return_to_service(nodes):
    """Resumes whichever of the nodes are out of service, and waits for them.

    scontrol rejects resuming a node that is already in service, so only the
    drained, down, or failing ones are named.

    Sending the resume is not the same as the node being back in service:
    resuming a DOWN node calls _require_node_reg(), which sets NO_RESPOND until
    its slurmd registers again. Waiting here is what makes the fixture's
    promise true, so a later test in this file, or the next file, does not
    inherit a node that is still out of service.
    """

    resumable = []
    for node in nodes:
        state = str(atf.get_node_parameter(node, "state") or "").upper()
        if any(s in state for s in OUT_OF_SERVICE_STATES):
            resumable.append(node)

    if resumable:
        atf.run_command(
            f"scontrol update nodename={','.join(resumable)} state=resume",
            user=atf.properties["slurm-user"],
            quiet=True,
            fatal=True,
        )

    atf.wait_for_node_state(
        nodes, list(OUT_OF_SERVICE_STATES), reverse=True, fatal=True
    )


def _event_rows(node):
    """Returns a node's accounting events as a list of split fields.

    The queried format is nodename|reason|tres|end|state|start. An open event
    reports an End of "Unknown"; that is the time_end=0 test.

    "all_time" is what keeps this from flaking: without it sacctmgr asks the
    database for "time_start < now", which hides an event until the clock ticks
    past the second it was opened in. TresReference is deliberately not asked
    for, so this runs against a Slurm that predates that field.
    """

    output = atf.run_command_output(
        f"sacctmgr -n -P list events all_time Event=Node Nodes={node} "
        f"format=nodename,reason%512,tres,end,state,start",
        user=atf.properties["slurm-user"],
        # sacctmgr exits 1 on an unrecognized format field. Fail loudly here
        # rather than returning nothing and letting the polls below time out.
        fatal=True,
        quiet=True,
    )

    return [line.split("|") for line in output.splitlines() if line.strip()]


def _scontrol_update(node, **fields):
    """Runs scontrol update for one node, quoting values that need it."""

    parts = [f"scontrol update nodename={node}"]
    for key, value in fields.items():
        parts.append(f"{key}={shlex.quote(str(value))}")
    atf.run_command(
        " ".join(parts),
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _wait_open(node, reason):
    """Polls until the node has an open event with this reason."""

    event = None
    for _ in atf.timer(fatal=True):
        event = _open_event(node, reason)
        if event is not None:
            break
    return event


def _wait_no_open_event(node):
    """Polls until the node has no open event.

    A resume closes every open row for the node, but the fixture that sends it
    does not wait for the database to catch up. Callers that snapshot the
    node's rows need that to have landed first.
    """

    for _ in atf.timer(fatal=True):
        if not any(row[3] == "Unknown" for row in _event_rows(node)):
            break


def _sleep_to_second_boundary():
    """Sleeps until just past the next second boundary.

    Two node events collide only when slurmdbd stamps them with the same
    time_t, so updates that have to collide are started at the top of a second
    to give them the whole second to land in.
    """

    delay = 1.05 - (time.time() % 1.0)
    if delay > 0:
        time.sleep(delay)


def _wait_barrier(barrier_node, reason):
    """Drains another node so earlier accounting RPCs have been applied."""

    # Sleep to avoid a known issue fixed in 26.11 (!4273) and tested in
    # test_same_second_reopen_stores_the_last_event.
    _sleep_to_second_boundary()
    _scontrol_update(barrier_node, state="drain", reason=reason)
    _wait_open(barrier_node, reason)


def _reason_matches(stored, expected):
    """True if sacctmgr's reason is the one we opened the event with.

    slurm_add_slash_to_quotes() puts a backslash before the apostrophe in the
    SQL literal. Depending on sql_mode that backslash is stored, so sacctmgr
    may print can\\'t when the admin typed can't.
    """

    return stored == expected or stored.replace("\\'", "'") == expected


def _open_event(node, reason):
    """Returns the node's open event with this reason, or None."""

    for row in _event_rows(node):
        if row[3] == "Unknown" and _reason_matches(row[1], reason):
            return row
    return None


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="An open node event was closed by an instance update before 26.11",
)
def test_open_node_event_survives_instance_update(nodes):
    """Verify an InstanceId update leaves a node's open event alone.

    A node that has an open event and has never been powered down used to count
    as never seen, because as_mysql_node_update() looked only for a powered
    down event. It then synthesized a node_down/node_up pair to give the
    instance update below it a row to land on, and that pair closed the open
    interval: the node read as back in service while it was still drained.
    """

    node, barrier_node = nodes[0], nodes[1]

    atf.run_command(
        f"scontrol update nodename={node} state=drain reason={DRAIN_REASON}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    event = _wait_open(node, DRAIN_REASON)
    start = event[5]
    rows_before = len(_event_rows(node))

    # Only the instance fields reach accounting. Extra= is deliberately left
    # out of that, so it is no use here.
    atf.run_command(
        f"scontrol update nodename={node} "
        f"InstanceId=i-108-14 InstanceType=t-108-14",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Nothing should change, and there is no polling for that. Drain the other
    # node and wait for its event instead: the controller sends both to the
    # database over one ordered queue, so once this one has landed the instance
    # update ahead of it has been applied too.
    atf.run_command(
        f"scontrol update nodename={barrier_node} state=drain "
        f"reason={BARRIER_REASON}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _wait_open(barrier_node, BARRIER_REASON)

    event = _open_event(node, DRAIN_REASON)
    assert (
        event is not None
    ), f"the instance update closed the node's open event: {_event_rows(node)!r}"

    # An event that was closed and reopened still reads as open, so the start is
    # what tells the original interval from a replacement.
    assert (
        event[5] == start
    ), f"the instance update restarted the node's open event: {_event_rows(node)!r}"

    assert (
        len(_event_rows(node)) == rows_before
    ), f"the instance update added an event: {_event_rows(node)!r}"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="A node event reason longer than 255 bytes was truncated before 26.11",
)
def test_node_event_reason_longer_than_tinytext(nodes):
    """Verify a drain reason longer than 255 bytes is stored whole.

    The event table's reason column was tinytext, so a longer reason was
    stored truncated. The open interval is matched by comparing the reason
    against what the row holds, and a truncated one never compares equal, so
    every later node_down reopened the event instead of leaving it running.
    """

    node = nodes[0]
    assert (
        len(LONG_REASON.encode()) > 255
    ), "LONG_REASON must exceed the former tinytext 255-byte limit"

    _scontrol_update(node, state="drain", reason=LONG_REASON)
    event = _wait_open(node, LONG_REASON)
    assert (
        event[1] == LONG_REASON
    ), f"reason was truncated: {event[1]!r} vs {LONG_REASON!r}"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="A same-second collision kept the first event's state before 26.11",
)
def test_same_second_down_then_drain_stores_the_drain_state(nodes):
    """Verify a same-second collision stores the later event's state.

    A collision on (node_name, time_start) used to leave the row's state as
    the earlier event wrote it. This ordering is the one that shows it: the
    drain flag makes sacctmgr print DRAIN, while the state the down event
    wrote prints DOWN.

    The reverse ordering is deliberately not tested. After drain then down the
    row holds DOWN|DRAIN and node_state_string_compact() tests the drain flag
    before the base state, so sacctmgr prints DRAIN either way, and the
    pre-26.11 same-second path already rewrote Reason. Telling the two apart
    there needs StateRaw compared against a value learned from a separate
    probe, which NODE_STATE_NO_RESPOND makes unstable between runs.
    """

    node, barrier_node = nodes[0], nodes[1]
    down_reason = "evt_ord_down"
    drain_reason = "evt_ord_drain"

    _wait_no_open_event(node)
    before = {tuple(row) for row in _event_rows(node)}

    # Both updates have to reach slurmdbd inside one wall-clock second, because
    # time_t is what collides. atf.run_command pays a sudo and a login shell per
    # call, which is most of that second, so send both as one command.
    _sleep_to_second_boundary()
    atf.run_command(
        f"scontrol update nodename={node} state=down reason={down_reason}; "
        f"scontrol update nodename={node} state=drain reason={drain_reason}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _wait_barrier(barrier_node, "evt_ord_barrier")

    # Assert the collision rather than assume it. Two updates that straddle a
    # second boundary get two time_start values, collide with nothing, and leave
    # every assertion below satisfied on unfixed code as well.
    new_rows = [row for row in _event_rows(node) if tuple(row) not in before]
    starts = {row[5] for row in new_rows}
    assert len(starts) == 1, (
        "the two updates did not land in one second, so nothing collided and "
        f"this test proves nothing: {new_rows!r}"
    )

    open_rows = [row for row in new_rows if row[3] == "Unknown"]
    assert len(open_rows) == 1, f"expected one open event, got: {new_rows!r}"

    event = open_rows[0]
    assert _reason_matches(
        event[1], drain_reason
    ), f"the collision kept the first event's reason: {event!r}"
    assert (
        "DRAIN" in event[4].upper()
    ), f"open event kept the state the down event wrote: {event!r}"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="A collision with a closed node event resurrected it before 26.11",
)
def test_same_second_reopen_stores_the_last_event(nodes):
    """Verify an event opened on a second a closed event already used wins.

    Draining, resuming and draining again inside one second makes the insert in
    as_mysql_node_down() collide with the row the resume just closed. That is
    the "on duplicate key update" clause: with nothing open, the function does
    not take the same-time_start update branch above it, which is the branch the
    other same-second test reaches. Before 26.11 the clause set only time_end=0,
    so the earlier event came back as an interval with no end, still carrying
    its own reason.
    """

    node, barrier_node = nodes[0], nodes[1]
    first_reason = "evt_reopen_first"
    last_reason = "evt_reopen_last"

    _wait_no_open_event(node)
    before = {tuple(row) for row in _event_rows(node)}

    # Three updates, one shell invocation, one second: the resume closes the row
    # the first drain opened, and the second drain then collides with it.
    _sleep_to_second_boundary()
    atf.run_command(
        f"scontrol update nodename={node} state=drain reason={first_reason}; "
        f"scontrol update nodename={node} state=resume; "
        f"scontrol update nodename={node} state=drain reason={last_reason}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _wait_barrier(barrier_node, "evt_reopen_barrier")

    # One Start across the new rows is the collision. Two means the updates
    # straddled a boundary, the insert hit no duplicate key, and the assertions
    # below would hold on unfixed code too.
    new_rows = [row for row in _event_rows(node) if tuple(row) not in before]
    starts = {row[5] for row in new_rows}
    assert len(starts) == 1, (
        "the three updates did not land in one second, so the insert collided "
        f"with nothing and this test proves nothing: {new_rows!r}"
    )

    event = new_rows[0]
    assert event[3] == "Unknown", f"the reopened event is not open: {event!r}"
    assert _reason_matches(
        event[1], last_reason
    ), f"the collision resurrected the earlier event: {event!r}"


def test_instance_update_does_not_book_one_second_downtime(nodes):
    """Verify an instance update books no node down time.

    as_mysql_node_update() used a separate time(NULL) for the node_down and
    node_up it synthesizes when the node has no events. Those calls can
    straddle a second boundary and book a real one-second whole-node down.

    This is not gated on a version, because it cannot fail before the fix in
    any useful fraction of runs: the two time(NULL) calls are separated only by
    as_mysql_node_down()'s MySQL round trips, so the unfixed code produces the
    same zero-duration pair unless that sub-millisecond window happens to
    contain a second boundary. Forcing the boundary would mean driving it from
    inside slurmdbd, which nothing here can do. It stands as a quiet invariant
    against a second timestamp being reintroduced.
    """

    node, barrier_node = nodes[0], nodes[1]

    # The in_service fixture resumes without waiting for the close to reach the
    # database. Until it has, the row it closes still reads End=Unknown, so the
    # snapshot below would capture the open form and the set difference would
    # then treat the same row, now closed, as an event this test caused.
    _wait_no_open_event(node)
    before = [tuple(row) for row in _event_rows(node)]

    # A node with no open interval and no powered-down row counts as never
    # seen. The in_service fixture already resumed, so this instance update
    # is what synthesizes the pair.
    _scontrol_update(node, InstanceId="i-108-14t", InstanceType="t-108-14t")
    _wait_barrier(barrier_node, "evt_time_barrier")

    # Whether a pair is synthesized at all is not the promise, and requiring
    # one makes this fail for reasons unrelated to the fix: the probe in
    # as_mysql_node_update() has no time bound, so any powered down event the
    # node has ever had suppresses the synthesis, as does a cluster running
    # power save. What has to hold either way is that the update books no time.
    # A zero-duration pair has the same start and end. A straddle books 1s.
    for row in _event_rows(node):
        if tuple(row) in before:
            continue
        end, start = row[3], row[5]
        assert end != "Unknown", f"instance update left an event open: {row!r}"
        assert start == end, f"instance update booked downtime: {row!r}"
