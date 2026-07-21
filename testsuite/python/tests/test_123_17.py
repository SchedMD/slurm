############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify that reservation accounting records survive dynamic-node churn.

Regression test for ticket 25553: Each dynamic node creation or deletion
triggered the creation of a new resv_table record, even though the reservation
itself hadn't changed. For long-running reservations the number of rows could
grow enough to degrade sacct queries on jobs or users using those reservations.

Node list changes also close the current cluster node snapshot in the event
table, so this covers filtering reservations by node across such a change. Once
a reservation keeps a single record, that record can be older than any snapshot
a query looks at. Both consumers of that filter are covered, along with a
reservation that has not started yet, which no snapshot can describe.
"""

import os
import time

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
dyn_node = f"dyn_{test_name}"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (25, 11),
        "sbin/slurmctld",
        reason="Ticket 25553: reservation re-record fix only in 25.11+",
    )
    atf.require_auto_config("wants dynamic nodes and accounting")
    atf.require_config_parameter("MaxNodeCount", 32)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_accounting(modify=True)
    atf.require_nodes(1)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def cluster_name():
    return atf.get_config_parameter("ClusterName")


@pytest.fixture(scope="module")
def resv_node():
    """The static node that the reservations are created on."""
    return next(iter(atf.get_nodes()))


@pytest.fixture
def create_reservation(request, resv_node):
    """Creates a permanent reservation named uniquely for the current test.

    The accounting records of a deleted reservation outlive it, so each test
    gets its own reservation name to keep the records of one from being picked
    up by another. A test that creates more than one is given a distinct name
    for each of them.
    """
    slurm_user = atf.properties["slurm-user"]
    created = []

    def create(start="now", user=None):
        res_name = f"res_{request.node.name}_{len(created)}"

        # Create a permanent reservation on specific nodes, as in the reported
        # scenario.
        atf.run_command(
            f"scontrol create reservationname={res_name} start={start} "
            f"duration=UNLIMITED nodes={resv_node} "
            f"user={user if user else atf.properties['test-user']} "
            f"flags=FLEX,OVERLAP,IGNORE_JOBS,MAGNETIC",
            user=slurm_user,
            fatal=True,
        )
        created.append(res_name)

        if start == "now":
            for _ in atf.timer(fatal=True):
                if atf.get_reservation_parameter(res_name, "State") == "ACTIVE":
                    break

        return res_name

    yield create

    for res_name in created:
        # The reservations are permanent and MAGNETIC on the cluster's only
        # node, so a delete that silently failed would attract later tests'
        # jobs.
        atf.run_command(
            f"scontrol delete reservationname={res_name}",
            user=slurm_user,
            fatal=True,
            quiet=True,
        )


@pytest.fixture
def churn_dynamic_node():
    slurm_user = atf.properties["slurm-user"]

    def churn(cycles):
        for _ in range(cycles):
            atf.run_command(
                f"scontrol create NodeName={dyn_node} State=CLOUD",
                user=slurm_user,
                fatal=True,
            )
            # Sleep at least 2 seconds after each node create/delete to result
            # in a distinct (id_resv, time_start) value.
            time.sleep(2)
            atf.run_command(
                f"scontrol delete NodeName={dyn_node}",
                user=slurm_user,
                fatal=True,
            )
            time.sleep(2)

    yield churn

    # In case a mid-cycle failure left the node behind
    if dyn_node in atf.get_nodes(quiet=True):
        atf.run_command(
            f"scontrol delete NodeName={dyn_node}",
            user=slurm_user,
            fatal=True,
            quiet=True,
        )


def get_dbd_resv_rows(res_name, end=None):
    """One output line per resv_table time-period row for a reservation.

    An explicit start narrows the window to this run. sacctmgr's default is
    00:00:00 of the previous day, which would pick up rows left behind by an
    aborted earlier run, since the reservation name is stable across runs.
    Giving a start without an end ends the period now, so an end in the future
    is needed to see a reservation that has not started yet.
    """
    output = atf.run_command_output(
        f"sacctmgr -Pn show reservation names={res_name} "
        f"start=now-5minutes {f'end={end} ' if end else ''}"
        "format=name,start,end",
        fatal=True,
    )
    return [line for line in output.splitlines() if line.strip()]


def get_dbd_resv_rows_for_nodes(res_name, nodes, cluster, end=None):
    """Same, but filtered by node.

    start=now limits the results to reservations that span now. A reservation
    can span now and still have been recorded before the cluster node snapshot
    that is open in the event table, which is the case that broke: the filter
    recovers the node list from a node index, and that index only resolves
    against the snapshot the record was written under. A cluster is required
    because a node index means nothing against more than one.
    """
    output = atf.run_command_output(
        f"sacctmgr -Pn show reservation names={res_name} nodes={nodes} "
        f"clusters={cluster} start=now {f'end={end} ' if end else ''}"
        "format=name,start,nodenames",
        fatal=True,
    )
    return [line for line in output.splitlines() if line.strip()]


def get_sreport_resv_rows(res_name, nodes, cluster, end):
    """One output line per reservation matched by an sreport node filter.

    sreport is the other consumer of the node filter, and it always applies a
    reporting period. Its default one ends at 23:59:59 of the previous day, so
    the period has to be given to see a reservation that is running now.
    """
    output = atf.run_command_output(
        f"sreport -Pn reservation utilization cluster={cluster} "
        f"names={res_name} nodes={nodes} start=now end={end} "
        "format=Name,Nodes -tSeconds",
        fatal=True,
    )
    return [line for line in output.splitlines() if line.strip()]


def test_node_churn_single_resv_record(create_reservation, churn_dynamic_node):
    """Node-list churn must not create new resv_table rows or move StartTime."""

    res_name = create_reservation()

    # Wait for the reservation's initial record to reach the database
    for _ in atf.timer(fatal=True):
        rows = get_dbd_resv_rows(res_name)
        if len(rows) == 1:
            break
    initial_row = rows[0]
    initial_start = atf.get_reservation_parameter(res_name, "StartTime")

    # Node churn causes slurmctld to report the cluster node list to slurmdbd
    # via DBD_CLUSTER_TRES, which returns ACCOUNTING_NODES_CHANGE_DB. Then
    # slurmctld responds by re-recording every reservation.
    churn_dynamic_node(3)

    # Poll slurmdbd for a bit and verify that the reservation doesn't get any
    # new time periods
    for _ in atf.timer(timeout=10, xfail=True):
        rows = get_dbd_resv_rows(res_name)
        assert len(rows) == 1, (
            f"Reservation {res_name} should have exactly one accounting "
            f"record after node-list churn, got {len(rows)}: {rows}"
        )

    assert get_dbd_resv_rows(res_name)[0] == initial_row, (
        f"The reservation accounting record should not be changed by "
        f"node-list churn (was '{initial_row}', "
        f"now '{get_dbd_resv_rows(res_name)[0]}')"
    )

    assert atf.get_reservation_parameter(res_name, "StartTime") == initial_start, (
        f"The reservation's StartTime should not be changed by "
        f"node-list churn (was {initial_start})"
    )


def test_resv_change_adds_record(create_reservation):
    """A change to a started reservation itself must still add a record.

    This is the control for node-list churn not adding one: what keeps churn
    out of the table must not keep a real change out of it as well.
    """
    slurm_user = atf.properties["slurm-user"]
    res_name = create_reservation()

    for _ in atf.timer(fatal=True):
        rows = get_dbd_resv_rows(res_name)
        if len(rows) == 1:
            break

    # A record is keyed on a start time in whole seconds, so a change in the
    # same second as the start would update the record instead.
    time.sleep(2)
    atf.run_command(
        f"scontrol update reservationname={res_name} comment={test_name}",
        user=slurm_user,
        fatal=True,
    )

    for _ in atf.timer(fatal=True):
        rows = get_dbd_resv_rows(res_name)
        if len(rows) != 1:
            break
    assert len(rows) == 2, (
        f"Changing started reservation {res_name} should close its accounting "
        f"record and add one more, got: {rows}"
    )


def test_node_filter_after_node_churn(
    create_reservation, churn_dynamic_node, resv_node, cluster_name
):
    """A node filter must match a reservation older than the node snapshot."""

    res_name = create_reservation()

    # Wait for the reservation's record to reach the database
    for _ in atf.timer(fatal=True):
        if len(get_dbd_resv_rows(res_name)) == 1:
            break

    # Each node create/delete closes the open cluster node snapshot in the
    # event table and opens a new one, so the snapshot that was current when
    # the reservation record was written is no longer the current one.
    churn_dynamic_node(1)

    start = atf.get_reservation_parameter(res_name, "StartTime")
    rows = get_dbd_resv_rows_for_nodes(res_name, resv_node, cluster_name)
    assert rows == [f"{res_name}|{start}|{resv_node}"], (
        f"Reservation {res_name} is on {resv_node}, so it should still match a "
        f"filter for that node after node-list churn, got: {rows}"
    )

    # Ask for a node that has never been part of the cluster. Without this the
    # assertion above would also pass if node filtering were skipped entirely,
    # which is what happened when an asked node was in no node snapshot.
    rows = get_dbd_resv_rows_for_nodes(res_name, f"not{dyn_node}", cluster_name)
    assert len(rows) == 0, (
        f"Reservation {res_name} is on {resv_node}, so it should not match a "
        f"filter for a node that does not exist, got: {rows}"
    )


def test_sreport_node_filter_after_node_churn(
    create_reservation, churn_dynamic_node, resv_node, cluster_name
):
    """sreport must match a reservation recorded before the snapshot too."""

    res_name = create_reservation()

    # Wait for the reservation's record to reach the database
    for _ in atf.timer(fatal=True):
        if len(get_dbd_resv_rows(res_name)) == 1:
            break

    churn_dynamic_node(1)

    rows = get_sreport_resv_rows(res_name, resv_node, cluster_name, "now+1hour")
    assert rows == [f"{res_name}|{resv_node}"], (
        f"Reservation {res_name} is on {resv_node}, so sreport should still "
        f"match a filter for that node after node-list churn, got: {rows}"
    )

    rows = get_sreport_resv_rows(res_name, f"not{dyn_node}", cluster_name, "now+1hour")
    assert len(rows) == 0, (
        f"Reservation {res_name} is on {resv_node}, so sreport should not "
        f"match a filter for a node that does not exist, got: {rows}"
    )


def test_node_filter_on_pending_reservation(
    create_reservation, resv_node, cluster_name
):
    """A node filter must match a reservation that has not started yet."""

    res_name = create_reservation(start="now+1hour")

    # The query period has to reach the reservation for its record to be
    # visible at all, since a period ending now excludes a future time period.
    for _ in atf.timer(fatal=True):
        if len(get_dbd_resv_rows(res_name, end="now+2hours")) == 1:
            break

    rows = get_dbd_resv_rows_for_nodes(
        res_name, resv_node, cluster_name, end="now+2hours"
    )
    assert len(rows) == 1, (
        f"Reservation {res_name} is on {resv_node}, so it should match a "
        f"filter for that node before it starts, got: {rows}"
    )

    # Documented in sreport.1: sreport reduces an end later than now to now,
    # but a period starting at the reservation's own start time still reaches
    # it.
    start = atf.get_reservation_parameter(res_name, "StartTime")
    output = atf.run_command_output(
        f"sreport -Pn reservation utilization cluster={cluster_name} "
        f"names={res_name} nodes={resv_node} start={start} "
        "format=Name,Nodes -tSeconds",
        fatal=True,
    )
    rows = [line for line in output.splitlines() if line.strip()]
    assert rows == [f"{res_name}|{resv_node}"], (
        f"Reservation {res_name} is on {resv_node}, so sreport should match a "
        f"filter for that node from its start time {start}, got: {rows}"
    )

    # Documented in sacctmgr.1: leaving the end of the period unset ends it
    # now, which hides a reservation that has not started yet.
    rows = get_dbd_resv_rows_for_nodes(res_name, resv_node, cluster_name)
    assert len(rows) == 0, (
        f"Reservation {res_name} starts in the future, so it should not be "
        f"matched by a query period ending now, got: {rows}"
    )
