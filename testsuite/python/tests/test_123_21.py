############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify that a reservation update with no record to follow ends the last one.

Regression test for ticket 25586: when slurmdbd gets an update for a record of
a reservation that it doesn't have, it adds a new record for the update. It
left the record before that one open, so the two overlapped, and reports that
add up the records of a reservation counted the overlapping time twice.

An update is lost on its way to slurmdbd for this to happen, which a test can't
arrange. The test undoes one in the database instead, so that the next update
names a record that isn't there.

Records that already overlap, from before the fix, are covered as well: the
usage rollup has to count each record only up to the start of the next one.
"""

import os
import shlex
import time

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
res_name = f"res_{test_name}"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmdbd",
        reason="Ticket 25586: missed reservation update fix is 26.11 and later",
    )
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def run_sql(sql_statement_repeat):
    resv_table = f"`{atf.get_config_parameter('ClusterName')}_resv_table`"

    def run(statement):
        return atf.run_command_output(
            f"{sql_statement_repeat} -N -B -e "
            f"{shlex.quote(statement.format(resv_table=resv_table))}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )

    return run


@pytest.fixture
def reservation():
    slurm_user = atf.properties["slurm-user"]
    node = next(iter(atf.get_nodes()))

    atf.run_command(
        f"scontrol create reservationname={res_name} start=now "
        f"duration=UNLIMITED nodes={node} "
        f"user={atf.properties['test-user']} "
        f"flags=FLEX,OVERLAP,IGNORE_JOBS",
        user=slurm_user,
        fatal=True,
    )

    yield res_name

    atf.run_command(
        f"scontrol delete reservationname={res_name}",
        user=slurm_user,
        fatal=True,
        quiet=True,
    )


def test_missed_update_ends_last_record(reservation, run_sql):
    """Records added for an update with no record to follow must not overlap."""

    for _ in atf.timer(fatal=True):
        output = run_sql(
            f"select id_resv from {{resv_table}} "
            f"where resv_name='{res_name}' and deleted=0 limit 1"
        ).split()
        if output:
            break
    id_resv = output[0]

    def get_records():
        output = run_sql(
            f"select time_start, time_end from {{resv_table}} "
            f"where id_resv={id_resv} and deleted=0 order by time_start"
        )
        return [tuple(map(int, line.split())) for line in output.splitlines()]

    # A record is keyed on a start time in whole seconds, and a change to a
    # started reservation moves its start to now. Each change has to come in a
    # later second than the one before it to add a record of its own.
    for n in (1, 2):
        time.sleep(2)
        atf.run_command(
            f"scontrol update reservationname={res_name} comment={test_name}_{n}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        for _ in atf.timer(fatal=True):
            records = get_records()
            if len(records) == n + 1:
                break

    # Undo the last update the way losing it would have: drop the record it
    # added and reopen the one before it. slurmctld still has the start of the
    # dropped record as the one the next update follows.
    first, second, third = records
    assert first[1] == second[0] and second[1] == third[0], (
        f"Each update should have ended the record before it where the next "
        f"one starts, got: {records}"
    )
    run_sql(
        f"delete from {{resv_table}} where id_resv={id_resv} "
        f"and time_start={third[0]}; "
        f"update {{resv_table}} set time_end={third[1]} "
        f"where id_resv={id_resv} and time_start={second[0]}"
    )
    assert get_records() == [
        first,
        (second[0], third[1]),
    ], "The undo should have left only the second record open"

    time.sleep(2)
    atf.run_command(
        f"scontrol update reservationname={res_name} comment={test_name}_3",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    for _ in atf.timer(fatal=True):
        records = get_records()
        if len(records) == 3:
            break

    fourth_start = records[2][0]
    assert fourth_start > third[0], (
        f"The added record should start after the dropped one did "
        f"({third[0]}), got: {records}"
    )
    assert records[0] == first, (
        f"The record that had already ended should be left as it was "
        f"({first}), got: {records}"
    )
    assert records[1] == (second[0], fourth_start), (
        f"The open record should end where the added one starts, so that the "
        f"records don't overlap, got: {records}"
    )


@pytest.fixture
def overlapping_records(run_sql):
    """Returns a function that writes two overlapping open records.

    The records are of a reservation slurmctld doesn't have, the way a record
    left open by a missed update and the one added for it would be. The id is
    far above any that slurmctld hands out. Both end where the rolled-up hour
    does, so that slurmdbd's own hourly rollups don't add to them later.
    """
    id_resv = 4000000000

    def write(starts, end):
        run_sql(f"delete from {{resv_table}} where id_resv={id_resv}")
        for start in starts:
            run_sql(
                f"insert into {{resv_table}} "
                f"(id_resv, resv_name, tres, time_start, time_end) values "
                f"({id_resv}, 'res_{test_name}_overlap', '1=1', {start}, {end})"
            )
        return id_resv

    yield write

    run_sql(f"delete from {{resv_table}} where id_resv={id_resv}")


@pytest.mark.parametrize(
    "offsets, unused",
    [
        # Both records start in the hour that is rolled up. Each counts up to
        # the start of the next one, and the last one to the end of the hour.
        ((600, 1800), (1200, 1800)),
        # Both start before the hour. The first ends where the second starts,
        # so it counts for none of the hour, and the second for all of it.
        ((-1800, -600), (0, 3600)),
    ],
    ids=["both_in_hour", "both_before_hour"],
)
def test_rollup_clips_overlapping_records(
    overlapping_records, run_sql, offsets, unused
):
    """The rollup must not count time that the next record covers as well."""

    # slurmdbd rolls up whole hours of its local time, which aren't whole UTC
    # hours in every timezone. Take the last one that has ended.
    now = int(time.time())
    local = time.localtime(now)
    hour_start = now - local.tm_min * 60 - local.tm_sec - 3600
    starts = [hour_start + offset for offset in offsets]
    id_resv = overlapping_records(starts, hour_start + 3600)

    # The times are given as epoch seconds so that DST doesn't change the hour.
    atf.run_command(
        f"sacctmgr -i rollup uts{hour_start} uts{hour_start + 3600}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # With no jobs in the reservation, the time a record is counted for in the
    # hour is all left unused, and the rollup stores it with the record.
    output = run_sql(
        f"select unused_wall from {{resv_table}} "
        f"where id_resv={id_resv} order by time_start"
    )
    assert [float(wall) for wall in output.split()] == list(unused), (
        f"Each record should be counted from its start to the start of the "
        f"next one or the end of the hour, got: {output.split()}"
    )
