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
reservation that has not started yet, which no snapshot can describe, and the
documented behavior of asking about more than one cluster.

Reservations whose association list denies every user are covered as well,
since the list that gets stored with the record is then empty. So is a change
to those denials alone on a started reservation, which the stored list cannot
show but which must still add a record.
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
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 25553: reservation re-record fix is 26.11 and later",
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

    # The reservations are permanent and MAGNETIC on the cluster's only node,
    # so one left behind would attract later tests' jobs. Collect the failures
    # and report them after the loop: failing inside it would skip every
    # reservation after the first failure, leaking the ones it was most
    # important to remove.
    errors = []

    for res_name in created:
        results = atf.run_command(
            f"scontrol delete reservationname={res_name}",
            user=slurm_user,
            quiet=True,
        )
        if results["exit_code"] != 0:
            errors.append(
                f"Failed to delete reservation {res_name}: {results['stderr']}"
            )

    if errors:
        pytest.fail("; ".join(errors))


@pytest.fixture
def assoc_for_test_user():
    """Gives the test user an association for a reservation to record.

    Only users that have one are recorded with a reservation, so without this
    a deny-only reservation has no list for the denials to be taken out of,
    and the code that takes them out never runs at all.
    """
    slurm_user = atf.properties["slurm-user"]
    test_user = atf.properties["test-user"]
    account = f"acct_{test_name}"

    atf.run_command(
        f"sacctmgr -i add account {account}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={account}",
        user=slurm_user,
        fatal=True,
    )

    # A reservation records the associations that slurmctld knows of, so wait
    # for the new one to reach it before any reservation is created.
    for _ in atf.timer(fatal=True):
        if account in atf.run_command_output(
            f"scontrol show assoc_mgr users={test_user} flags=assoc",
            quiet=True,
        ):
            break

    yield

    atf.run_command(
        f"sacctmgr -i remove user {test_user} account={account}",
        user=slurm_user,
        fatal=True,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {account}",
        user=slurm_user,
        fatal=True,
        quiet=True,
    )


@pytest.fixture
def extra_cluster():
    """Registers a second cluster in the accounting database.

    Only the accounting tables of that cluster are needed, so nothing is run
    for it. Naming a cluster that was never added would not do, since a query
    then has no tables to read.
    """
    slurm_user = atf.properties["slurm-user"]
    cluster = f"cluster_{test_name}"

    atf.run_command(
        f"sacctmgr -i add cluster {cluster}",
        user=slurm_user,
        fatal=True,
    )

    yield cluster

    atf.run_command(
        f"sacctmgr -i remove cluster {cluster}",
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

    An explicit start narrows the window past sacctmgr's default of 00:00:00 of
    the previous day, which keeps out the rows of an earlier run that did
    delete its reservations, since deleting one sets time_end to the delete
    time. It does not keep out a row an aborted run left behind without
    deleting its reservation: the window is an overlap test, not a containment
    one, and a permanent reservation's time_end is a year out. The reservation
    name is stable across runs, so such a row is still picked up.

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


def get_dbd_resv_assocs(res_name):
    """One output line per resv_table row, holding its association list.

    The list is empty both for a reservation that never had one and for one
    whose entries were all denials, so telling those apart takes a
    reservation that is known to have a non-empty list to compare against.
    """
    output = atf.run_command_output(
        f"sacctmgr -Pn show reservation names={res_name} "
        "start=now-5minutes format=name,associations",
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

    rows = get_dbd_resv_rows(res_name)
    assert rows == [initial_row], (
        f"The reservation accounting record should not be changed by "
        f"node-list churn (was '{initial_row}', now: {rows})"
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


def test_sreport_node_filter_needs_one_cluster(
    create_reservation, resv_node, cluster_name, extra_cluster
):
    """sreport refuses a node filter unless one cluster is asked about.

    Naming several and asking about all of them are both refused, since a node
    index only means something against one cluster's node list either way.
    The refusal is checked by what comes back as well as by the exit code,
    since a report that was never filtered looks like an ordinary one.

    The refusal asserted here is printed by sreport, not by the controller the
    module gates on, and client commands are upgraded last. An older sreport
    sends the query and reports what it gets back in its own words.
    """
    # Not a skipif: sreport -V fails until accounting is configured by setup().
    atf.require_version(
        (26, 11),
        "bin/sreport",
        reason="Ticket 25553: sreport refuses a multi-cluster node filter in 26.11+",
    )

    res_name = create_reservation()

    for _ in atf.timer(fatal=True):
        if len(get_dbd_resv_rows(res_name)) == 1:
            break

    # A second cluster is registered, so all_clusters really is more than one.
    # It is a bare keyword rather than a value of cluster=, which is the only
    # spelling sreport recognizes for it.
    # No fatal=True: the failure is what is being asserted.
    for asked in (f"cluster={cluster_name},{extra_cluster}", "all_clusters"):
        result = atf.run_command(
            f"sreport -Pn reservation utilization {asked} names={res_name} "
            f"nodes={resv_node} start=now end=now+1hour format=Name,Nodes"
        )
        assert result["exit_code"] != 0, (
            f"Asking sreport about more than one cluster with a node filter "
            f"should fail ({asked}), got exit_code {result['exit_code']} and "
            f"stdout: {result['stdout']}"
        )
        assert "one cluster" in result["stderr"], (
            f"The refusal should say a node filter takes one cluster ({asked}), "
            f"got: {result['stderr']}"
        )
        assert res_name not in result["stdout"], (
            f"A refused query should report nothing ({asked}), but {res_name} "
            f"was reported: {result['stdout']}"
        )


def test_sreport_default_period_hides_running_reservation(
    create_reservation, resv_node, cluster_name
):
    """sreport's default reporting period does not reach a running reservation.

    The default period starts on the previous day and ends at 01:00 of the
    current one, so a reservation that started later today is outside it. An
    empty report then means the period rather than the nodes, which is worth
    pinning because an empty report was also what the node filter bug produced.
    """
    res_name = create_reservation()
    start = atf.get_reservation_parameter(res_name, "StartTime")

    for _ in atf.timer(fatal=True):
        if len(get_dbd_resv_rows(res_name)) == 1:
            break

    rows = get_sreport_resv_rows(res_name, resv_node, cluster_name, "now+1hour")
    assert rows == [f"{res_name}|{resv_node}"], (
        f"Reservation {res_name} is on {resv_node}, so sreport should match a "
        f"filter for that node when the period reaches it, got: {rows}"
    )

    # The same query without a period.
    output = atf.run_command_output(
        f"sreport -Pn reservation utilization cluster={cluster_name} "
        f"names={res_name} nodes={resv_node} format=Name,Nodes -tSeconds",
        fatal=True,
    )
    today = time.strftime("%Y-%m-%d")
    if not start.startswith(today) or start.startswith(f"{today}T00:"):
        pytest.skip(f"The default reporting period reaches {start}")

    rows = [line for line in output.splitlines() if line.strip()]
    assert len(rows) == 0, (
        f"Reservation {res_name} started at {start}, so the default reporting "
        f"period, which ends at 01:00 today, should not reach it, got: {rows}"
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


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Ticket 25553: sacctmgr refuses a multi-cluster node filter in 26.11+",
)
def test_node_filter_needs_one_cluster(
    create_reservation, resv_node, cluster_name, extra_cluster
):
    """A node filter is refused unless one cluster is asked about.

    The refusal asserted here is printed by sacctmgr, not by the controller the
    module gates on, and client commands are upgraded last. An older sacctmgr
    sends the query and reports what it gets back in its own words.
    """

    res_name = create_reservation()

    # Wait for the reservation's record to reach the database
    for _ in atf.timer(fatal=True):
        if len(get_dbd_resv_rows(res_name)) == 1:
            break

    # Asking one cluster about a node that has never been part of it excludes
    # the reservation, so the node that is asked for is what matters below.
    rows = get_dbd_resv_rows_for_nodes(res_name, f"not{dyn_node}", cluster_name)
    assert len(rows) == 0, (
        f"Reservation {res_name} is on {resv_node}, so it should not match a "
        f"filter for a node that does not exist, got: {rows}"
    )

    # Documented in sacctmgr.1 and sreport.1: a node index only means anything
    # against the node list of one cluster, so asking about more than one is
    # refused rather than answered without the filter. sacctmgr refuses before
    # querying, and slurmdbd refuses a request that reaches it anyway.
    #
    # No fatal=True here: the failure is what is being asserted, so failing the
    # test on it would invert this case.
    result = atf.run_command(
        f"sacctmgr -Pn show reservation names={res_name} "
        f"nodes=not{dyn_node} clusters={cluster_name},{extra_cluster} "
        f"start=now format=name,start,nodenames"
    )
    assert result["exit_code"] != 0, (
        f"Asking about more than one cluster with a node filter should fail, "
        f"got exit_code {result['exit_code']} and stdout: {result['stdout']}"
    )
    assert "one cluster" in result["stderr"], (
        f"The refusal should say a node filter takes one cluster, got: "
        f"{result['stderr']}"
    )


def test_deny_only_reservation_record(create_reservation, assoc_for_test_user):
    """A reservation whose association list is entirely denials is recorded."""

    test_user = atf.properties["test-user"]

    # An allowed user is recorded as an association. This is the control for
    # the deny-only reservation below, whose list is empty because the
    # denials were taken out of it rather than because it never had one.
    allow_name = create_reservation()
    for _ in atf.timer(fatal=True):
        allow_rows = get_dbd_resv_assocs(allow_name)
        if len(allow_rows) == 1:
            break
    assert allow_rows != [f"{allow_name}|"], (
        f"Reservation {allow_name} allows {test_user}, who has an "
        f"association, so one should be recorded with it, got: {allow_rows}"
    )

    # A deny-only user list leaves nothing behind once the denials are removed
    # from the association list that is stored with the record, which used to
    # run the length of that list negative.
    deny_name = create_reservation(user=f"-{test_user}")

    assert (
        atf.get_reservation_parameter(deny_name, "Users") == f"-{test_user}"
    ), f"Reservation {deny_name} should deny {test_user}"

    for _ in atf.timer(fatal=True):
        deny_rows = get_dbd_resv_assocs(deny_name)
        if len(deny_rows) == 1:
            break
    assert deny_rows == [f"{deny_name}|"], (
        f"Reservation {deny_name} only denies {test_user}, so its recorded "
        f"association list should be empty, got: {deny_rows}"
    )


def test_deny_change_adds_record(create_reservation, assoc_for_test_user):
    """A change to the users a started reservation denies must add a record."""

    slurm_user = atf.properties["slurm-user"]
    test_user = atf.properties["test-user"]

    # root has an association on every cluster, so denying it puts an id in
    # the association list slurmctld keeps, even though the list stored with
    # the record leaves denials out.
    res_name = create_reservation(user="-root")

    # Giving an end keeps a row that starts in the current second in view.
    # Without one the query period ends now, and a record that the update
    # below starts now would be missing from a read made in the same second.
    for _ in atf.timer(fatal=True):
        rows = get_dbd_resv_rows(res_name, end="now+1hour")
        if len(rows) == 1:
            break
    initial_rows = rows
    initial_start = rows[0].split("|")[1]

    # A record is keyed on a start time in whole seconds, so a change in the
    # same second as the start would update the record instead.
    time.sleep(2)

    # On a deny list, -user adds the user to it.
    atf.run_command(
        f"scontrol update reservationname={res_name} users=-{test_user}",
        user=slurm_user,
        fatal=True,
    )
    assert (
        atf.get_reservation_parameter(res_name, "Users") == f"-root,-{test_user}"
    ), f"Reservation {res_name} should deny root and {test_user}"

    # slurmctld counts a change to the denied users as a change and moves the
    # accounting start of a started reservation to now, while the stored
    # record sees nothing different. Updating that record in place would move
    # its start forward too, losing the time it had already covered, so either
    # a new record or a moved start ends the wait, and only the first is right.
    for _ in atf.timer(fatal=True):
        rows = get_dbd_resv_rows(res_name, end="now+1hour")
        if rows != initial_rows:
            break
    assert len(rows) == 2, (
        f"Changing the users started reservation {res_name} denies should "
        f"close its accounting record and add one more, got: {rows} "
        f"(was {initial_rows})"
    )

    # scontrol's StartTime is the first start and does not move, so the check
    # that no time was lost is made on the records themselves.
    old_start, old_end = rows[0].split("|")[1:3]
    new_start = rows[1].split("|")[1]
    assert old_start == initial_start and old_end == new_start, (
        f"The first record of {res_name} should keep its start and end where "
        f"the new one starts, got: {rows} (was {initial_rows})"
    )
