############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Regression test for bug 25695: sacct Reservation at window boundaries.

sacct resolves a job's reservation name by joining job_table.id_resv against
resv_table, whose primary key is (id_resv, time_start), so the join has to
pick a record by time.  A reservation is active from time_start inclusive:
job_test_resv() clamps a queued job's earliest start to exactly
resv.start_time, so a job whose start lands on that first second is
ordinary, and a join requiring "resv.time_start < job.time_start" strictly
drops it, leaving Reservation blank while ReservationId, ReqReservation and
squeue still show the reservation.  That is the defect this test holds
closed.

Rows are seeded directly in MySQL because the failure turns on second-level
coincidences between a job's start and a reservation record's boundaries,
which cannot be produced reliably by submitting real jobs.  Seeding proves
the join; it does not prove slurmdbd writes this shape, which no test
currently covers.

Every case asserts Reservation together with ReservationId, because the
reported defect was those two disagreeing while ReqReservation and squeue
still named the reservation.  ReqReservation is asserted only where a case
turns on it, since it is read straight from job_table and never passes
through the join.

Seeding goes through conftest's mysql helper.

Run this only against a throwaway accounting DB.
"""

import collections
import time

import pytest

import atf

_CLUSTER = "test1016resv"
_ACCOUNT = "test1016acct"
_USER = "root"

_NO_VAL = 0xFFFFFFFE

# Slurm job states (enum job_states)
_JOB_PENDING = 0
_JOB_COMPLETE = 3

# Single-record reservation covering the whole window.
_RESV_ID = 1
_RESV_NAME = "resv1016"

# Split reservation: two records that tile the same window, as
# as_mysql_modify_resv() produces when an active reservation changes.  Real
# split records share a name; these differ only so an assertion can tell
# which record the join selected.
_SPLIT_ID = 2
_SPLIT_EARLY_NAME = "resv1016a"
_SPLIT_LATE_NAME = "resv1016b"

# Second reservation, used as the one a multi-reservation job actually ran
# in.  Seeded with the same window as _RESV_ID, so a join keyed on time
# instead of id_resv would match both.
_ALT_ID = 3
_ALT_NAME = "resv1016alt"

# An id with no resv_table record at all, as a PurgeResvAfter shorter than
# PurgeJobAfter leaves behind.
_DANGLING_ID = 99

_WINDOW = 3600
_HALF = _WINDOW // 2

# Job ids, each also used as job_db_inx.
_JOB_AT_START = 1
_JOB_INSIDE = 2
_JOB_LAST_SECOND = 3
_JOB_AT_END = 4
_JOB_BEFORE_START = 5
_JOB_PENDING_AT_START = 6
_JOB_SPLIT_BOUNDARY = 7
_JOB_SPLIT_EARLY = 8
_JOB_PENDING_BEFORE_RESV = 9
_JOB_NO_RESV = 10
_JOB_DANGLING_RESV = 11
_JOB_MULTI_STARTED = 12
_JOB_MULTI_PENDING = 13

_RESET_TABLES = ["job_table", "resv_table"]

_Row = collections.namedtuple("_Row", "jobid reservation reservation_id req")


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmdbd",
        reason="Ticket 25695: sacct dropped the reservation name for a job "
        "starting in the reservation's first second. Fixed in 26.11",
    )
    atf.require_accounting(modify=True)
    # Outside auto-config the conftest mysql helper is None, so every seeding
    # call would shell out "None -e ...", failing opaquely and leaving the
    # synthetic cluster behind with no database restore to undo it.
    # require_accounting() only gates that when allow-slurmdbd-modify is
    # unset, so it is not sufficient on its own.
    atf.require_auto_config("seeds rows directly into the accounting database")
    # Seeded rows sit in the past; a purge pass mid-test would delete them.
    atf.require_config_parameter("PurgeJobAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeResvAfter", None, source="slurmdbd")
    atf.require_slurm_running()


def _sql(mysql_cmd, stmt, fatal=True, quiet=False):
    atf.run_command(
        f'{mysql_cmd} -e "{stmt.replace("`", r"\`")}"',
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=quiet,
    )


@pytest.fixture(scope="module")
def seeded_db(request, setup, sql_statement_repeat):
    """Create the test cluster and seed every case once.

    slurmdbd caches association and user data but reads job_table and
    resv_table per query, so the seeded rows are visible without a restart.
    """
    slurm_user = atf.properties["slurm-user"]

    # Best effort: the per-cluster tables do not exist on a fresh database,
    # and a previous interrupted run may have left rows at these keys.
    for table in _RESET_TABLES:
        _sql(
            sql_statement_repeat,
            f"TRUNCATE TABLE {_CLUSTER}_{table}",
            fatal=False,
            quiet=True,
        )
    # Not fatal: on a fresh database there is no cluster to remove yet.
    atf.run_command(
        f"sacctmgr -i remove cluster {_CLUSTER}", user=slurm_user, quiet=True
    )

    atf.run_command(f"sacctmgr -i add cluster {_CLUSTER}", user=slurm_user, fatal=True)

    def _teardown():
        # A record with time_end=0 counts as an active job and makes the
        # cluster removal refuse, so drop the jobs first.  Not fatal, or a
        # failure here would abandon both removals below.
        _sql(
            sql_statement_repeat,
            f"TRUNCATE TABLE {_CLUSTER}_job_table",
            fatal=False,
        )
        atf.run_command(
            f"sacctmgr -i remove cluster {_CLUSTER}", user=slurm_user, fatal=True
        )
        # Accounts are not cluster-scoped, so removing the cluster leaves this.
        # Not fatal: on a setup that failed before the account was added there
        # is nothing here to remove.
        atf.run_command(f"sacctmgr -i remove account {_ACCOUNT}", user=slurm_user)

    # Registered before the account, the user and the seeded rows exist, so a
    # failure creating any of those still cleans up rather than leaking them.
    request.addfinalizer(_teardown)

    atf.run_command(
        f"sacctmgr -i add account {_ACCOUNT} cluster={_CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {_USER} account={_ACCOUNT} cluster={_CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    out = atf.run_command_output(
        f"sacctmgr -nP list assoc cluster={_CLUSTER} account={_ACCOUNT} "
        f"user={_USER} format=id",
        user=slurm_user,
        fatal=True,
    )
    if not out.strip():
        pytest.fail(f"no assoc for {_USER}/{_ACCOUNT} on {_CLUSTER}")
    assoc_id = int(out.strip().splitlines()[0].rstrip("|"))

    # Put the whole window comfortably in the past so nothing is still open.
    resv_start = int(time.time()) - 2 * _WINDOW
    resv_end = resv_start + _WINDOW

    for resv_id, name, start, end in [
        (_RESV_ID, _RESV_NAME, resv_start, resv_end),
        (_ALT_ID, _ALT_NAME, resv_start, resv_end),
        # Two records tiling one window: the earlier ends exactly where the
        # later begins, which is the invariant as_mysql_modify_resv() keeps.
        (_SPLIT_ID, _SPLIT_EARLY_NAME, resv_start, resv_start + _HALF),
        (_SPLIT_ID, _SPLIT_LATE_NAME, resv_start + _HALF, resv_end),
    ]:
        _sql(
            sql_statement_repeat,
            f"INSERT INTO `{_CLUSTER}_resv_table` "
            f"(id_resv, assoclist, nodelist, resv_name, "
            f"time_start, time_end, flags, unused_wall) "
            f"VALUES ({resv_id}, '{assoc_id}', 'n1', '{name}', "
            f"{start}, {end}, 0, 0.0)",
        )

    both = f"{_RESV_NAME},{_ALT_NAME}"
    early = _SPLIT_EARLY_NAME
    # job id, id_resv, resv_req, time_submit, time_start, time_end.
    # A zero time_start is a job that never ran.
    jobs = [
        # The reported failure: started in the reservation's first second.
        (_JOB_AT_START, _RESV_ID, _RESV_NAME, resv_start, resv_start, resv_end),
        (_JOB_INSIDE, _RESV_ID, _RESV_NAME, resv_start, resv_start + 1, resv_end),
        (_JOB_LAST_SECOND, _RESV_ID, _RESV_NAME, resv_start, resv_end - 1, resv_end),
        # Starts on the final record's time_end, i.e. once the reservation is
        # over; outside [time_start, time_end).
        (_JOB_AT_END, _RESV_ID, _RESV_NAME, resv_start, resv_end, resv_end + 1),
        (
            _JOB_BEFORE_START,
            _RESV_ID,
            _RESV_NAME,
            resv_start - 10,
            resv_start - 1,
            resv_start + 10,
        ),
        (
            _JOB_SPLIT_BOUNDARY,
            _SPLIT_ID,
            early,
            resv_start,
            resv_start + _HALF,
            resv_end,
        ),
        (
            _JOB_SPLIT_EARLY,
            _SPLIT_ID,
            early,
            resv_start,
            resv_start + 1,
            resv_start + 10,
        ),
        # Ran in the second of two requested reservations: Reservation must
        # name the one used, ReqReservation must still list both.
        (_JOB_MULTI_STARTED, _ALT_ID, both, resv_start, resv_start + 1, resv_end),
        # Ordinary job, no reservation at all.
        (_JOB_NO_RESV, 0, "", resv_start, resv_start + 1, resv_end),
        # id_resv pointing at a record that no longer exists, as a shorter
        # PurgeResvAfter than PurgeJobAfter leaves behind.
        (
            _JOB_DANGLING_RESV,
            _DANGLING_ID,
            _RESV_NAME,
            resv_start,
            resv_start + 1,
            resv_end,
        ),
        # Never started, so the join falls back to time_submit.  Submitted in
        # the reservation's first second.
        (_JOB_PENDING_AT_START, _RESV_ID, _RESV_NAME, resv_start, 0, 0),
        # Never started, submitted before the reservation began -- the
        # ordinary "queue a job for tomorrow's reservation" shape.
        (_JOB_PENDING_BEFORE_RESV, _RESV_ID, _RESV_NAME, resv_start - 10, 0, 0),
        # Never started, several reservations requested.
        (_JOB_MULTI_PENDING, _RESV_ID, both, resv_start - 10, 0, 0),
    ]
    for job_id, resv_id, resv_req, submit, start, end in jobs:
        state = _JOB_PENDING if start == 0 else _JOB_COMPLETE
        _sql(
            sql_statement_repeat,
            f"INSERT INTO `{_CLUSTER}_job_table` "
            f"(job_db_inx, id_assoc, id_wckey, id_resv, id_job, "
            f"id_user, id_group, het_job_id, het_job_offset, "
            f"state_reason_prev, nodes_alloc, `partition`, priority, state, "
            f"time_submit, time_eligible, time_start, time_end, "
            f"cpus_req, job_name, account, resv_req) "
            f"VALUES ({job_id}, {assoc_id}, 0, {resv_id}, {job_id}, "
            f"0, 0, 0, {_NO_VAL}, "
            f"0, 1, 'part1', 0, {state}, "
            f"{submit}, {submit}, {start}, {end}, "
            f"4, 'test1016', '{_ACCOUNT}', '{resv_req}')",
        )

    yield


def _sacct_row(job_id):
    """Return the single sacct row for job_id as a _Row.

    --duplicates is required: without it sacct keeps only one record per job
    id (sacct.1, -D), which would hide a join that matched several
    reservation records and make the cardinality check vacuous.  Asserting
    exactly one row is itself part of the contract -- sacct.1's -D text
    enumerates the causes of multiple rows per job id, and reservation
    record splits are not among them.
    """
    out = atf.run_command_output(
        f"sacct -M {_CLUSTER} -j {job_id} -X -n -P -D --allusers "
        f"--format=JobID,Reservation,ReservationId,ReqReservation",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    rows = []
    for line in out.splitlines():
        fields = line.split("|")
        # Guards against a short or unexpected line silently reading as
        # "no reservation" and passing the negative assertions.
        assert len(fields) == len(
            _Row._fields
        ), f"unparsable sacct row for job {job_id}: {line!r}"
        rows.append(_Row(*fields))
    assert (
        len(rows) == 1
    ), f"expected exactly one sacct row for job {job_id}, got {rows}"
    return rows[0]


@pytest.mark.parametrize(
    "job_id",
    [
        pytest.param(_JOB_AT_START, id="start_equals_resv_start"),
        pytest.param(_JOB_INSIDE, id="start_inside_window"),
        pytest.param(_JOB_LAST_SECOND, id="start_last_second"),
    ],
)
def test_started_job_resolves_reservation(seeded_db, job_id):
    """A job started within [time_start, time_end) resolves its name.

    Reservation and ReservationId must agree; the reported defect was
    Reservation coming back empty while ReservationId still named it.
    """
    row = _sacct_row(job_id)
    assert (
        row.reservation == _RESV_NAME
    ), f"job {job_id} reported Reservation '{row.reservation}', expected '{_RESV_NAME}'"
    assert row.reservation_id == str(_RESV_ID), (
        f"job {job_id} reported ReservationId '{row.reservation_id}', "
        f"expected '{_RESV_ID}'"
    )
    assert (
        row.req == _RESV_NAME
    ), f"job {job_id} reported ReqReservation '{row.req}', expected '{_RESV_NAME}'"


@pytest.mark.parametrize(
    "job_id",
    [
        pytest.param(_JOB_PENDING_AT_START, id="submit_equals_resv_start"),
        pytest.param(_JOB_PENDING_BEFORE_RESV, id="submit_before_resv_start"),
    ],
)
def test_pending_job_resolves_reservation(seeded_db, job_id):
    """A job that never started resolves its name from time_submit.

    submit_equals_resv_start goes through the clause this fix changed;
    submit_before_resv_start goes through the join's unbounded "record
    starts after the job was submitted" clause, which was not touched.
    """
    row = _sacct_row(job_id)
    assert (
        row.reservation == _RESV_NAME
    ), f"job {job_id} reported Reservation '{row.reservation}', expected '{_RESV_NAME}'"
    assert row.reservation_id == str(_RESV_ID), (
        f"job {job_id} reported ReservationId '{row.reservation_id}', "
        f"expected '{_RESV_ID}'"
    )


def test_multi_reservation_started_job_names_the_one_used(seeded_db):
    """A started job names the reservation it ran in, not the first requested.

    sbatch.1 (--reservation): "In accounting the first reservation will be
    seen and after the job starts the reservation used will replace it."
    This covers the accounting half only; which id_resv slurmctld records is
    not exercised here.
    """
    row = _sacct_row(_JOB_MULTI_STARTED)
    assert row.reservation == _ALT_NAME, (
        f"started job reported Reservation '{row.reservation}', expected the "
        f"reservation it ran in, '{_ALT_NAME}'"
    )
    # ReservationId is read from job_table.id_resv and never passes through
    # the join, so it only echoes the seeded value.  What proves the join is
    # keyed on id_resv rather than on time is the name above plus
    # _sacct_row()'s single-row check: _ALT_ID shares _RESV_ID's window, so a
    # time-keyed join would match several records for this job.
    assert row.reservation_id == str(_ALT_ID), (
        f"started job reported ReservationId '{row.reservation_id}', "
        f"expected '{_ALT_ID}'"
    )
    assert (
        row.req == f"{_RESV_NAME},{_ALT_NAME}"
    ), f"ReqReservation '{row.req}' should still list every requested reservation"


def test_multi_reservation_pending_job_lists_all_requested(seeded_db):
    """A job that never started lists every reservation it requested.

    Reservation follows id_resv, which the fixture supplies, so this does
    not verify sbatch.1's "the first reservation will be seen" -- that is a
    statement about what slurmctld records, and no test covers it.  What it
    does pin is that ReqReservation echoes the full requested list while
    Reservation resolves the single recorded id.
    """
    row = _sacct_row(_JOB_MULTI_PENDING)
    assert (
        row.reservation == _RESV_NAME
    ), f"pending job reported Reservation '{row.reservation}', expected '{_RESV_NAME}'"
    assert row.reservation_id == str(_RESV_ID), (
        f"pending job reported ReservationId '{row.reservation_id}', "
        f"expected '{_RESV_ID}'"
    )
    assert (
        row.req == f"{_RESV_NAME},{_ALT_NAME}"
    ), f"ReqReservation '{row.req}' should still list every requested reservation"


def test_job_without_reservation_reports_none(seeded_db):
    """An ordinary job reports no reservation in either field."""
    row = _sacct_row(_JOB_NO_RESV)
    assert (
        row.reservation == ""
    ), f"job with no reservation reported Reservation '{row.reservation}'"
    assert (
        row.reservation_id == ""
    ), f"job with no reservation reported ReservationId '{row.reservation_id}'"


def test_job_started_before_non_flex_reservation(seeded_db):
    """A job started before a non-FLEX reservation's record resolves no name.

    Scoped to a reservation with no flags set.  FLEX explicitly permits a job
    to begin before the reservation's start time (scontrol.1) and the join
    does not resolve that case either, which is a pre-existing gap tracked
    separately.  This job overlaps the reservation's window, so a FLEX fix
    keyed on overlap would change this expectation, while one keyed on the
    reservation's flags would leave it intact.
    """
    row = _sacct_row(_JOB_BEFORE_START)
    assert (
        row.reservation == ""
    ), f"job starting before the reservation reported '{row.reservation}'"
    # The id is still recorded even though the name does not resolve.
    assert row.reservation_id == str(_RESV_ID), (
        f"ReservationId '{row.reservation_id}' should still be recorded, "
        f"expected '{_RESV_ID}'"
    )


def test_job_started_at_resv_end_resolves_no_name(seeded_db):
    """Pins the join's end-exclusive convention, not a documented promise.

    Exactly one endpoint of a reservation record can be inclusive, or a job
    landing on an interior seam between two tiled records matches both.  The
    start is the inclusive one because the scheduler plans every queued job
    to start at exactly resv.start_time.  The cost is this case: a job whose
    start equals the final record's time_end resolves no name, while
    ReservationId still records it.

    The state is reachable, not hypothetical.  job_test_resv() rejects a
    candidate start only when it is strictly after end_time, and
    job_resv_check() keeps the reservation alive while end_time >= now.
    Deleting a reservation, or shortening its end into the current second,
    puts a record's time_end on a running job's start the same way.  A
    recurring reservation usually advances past the instant first, but not
    when it carries FLEX, which skips the advance, and not when its duration
    equals its recurrence period, where the advance leaves the next
    start_time equal to the old end_time.  Treat a failure here as a
    decision to revisit -- the cost is tracked, not an impossibility.
    """
    row = _sacct_row(_JOB_AT_END)
    assert (
        row.reservation == ""
    ), f"job starting at the reservation's end reported '{row.reservation}'"
    assert row.reservation_id == str(_RESV_ID), (
        f"ReservationId '{row.reservation_id}' should still be recorded, "
        f"expected '{_RESV_ID}'"
    )


def test_job_with_purged_reservation_record(seeded_db):
    """A job outliving its reservation record still lists, with no name.

    PurgeResvAfter and PurgeJobAfter are independent (slurmdbd.conf.5), so a
    job row can reference an id_resv whose record is gone.  The job must not
    disappear from sacct.
    """
    row = _sacct_row(_JOB_DANGLING_RESV)
    assert row.jobid == str(
        _JOB_DANGLING_RESV
    ), f"job with a purged reservation record went missing from sacct: {row}"
    assert (
        row.reservation == ""
    ), f"job with a purged reservation record reported '{row.reservation}'"
    assert row.reservation_id == str(_DANGLING_ID), (
        f"ReservationId '{row.reservation_id}' should still be recorded, "
        f"expected '{_DANGLING_ID}'"
    )


def test_split_records_match_started_job_exactly_once(seeded_db):
    """Tiled records must not duplicate or drop a job that has started.

    Only the started branch of the join is covered.  A job that never
    started can match several records through the join's unbounded "record
    starts after the job was submitted" clause, and the inclusive start adds
    one input to that: a time_submit equal to the earliest record's
    time_start.  Both rows carry the same name and the extra one only shows
    under sacct -D.  No case here covers it.
    """
    for job_id, expected in (
        (_JOB_SPLIT_BOUNDARY, _SPLIT_LATE_NAME),
        (_JOB_SPLIT_EARLY, _SPLIT_EARLY_NAME),
    ):
        row = _sacct_row(job_id)
        assert row.reservation == expected, (
            f"job {job_id} matched '{row.reservation}', expected the record "
            f"it ran under, '{expected}'"
        )
        assert row.reservation_id == str(_SPLIT_ID), (
            f"job {job_id} reported ReservationId '{row.reservation_id}', "
            f"expected '{_SPLIT_ID}'"
        )
