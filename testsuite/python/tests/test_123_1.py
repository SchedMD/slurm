############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import datetime
import logging
import re
import time

import pytest

import atf

pytestmark = pytest.mark.slow

# atf.get_version() shells out through sudo, so resolve it once.
CTLD_VERSION = atf.get_version("sbin/slurmctld")

node1 = ""
node2 = ""

# datetime.date.weekday() convention: Monday is 0, Sunday is 6.
#
# These helpers resolve weekdays with the client's local time, while slurmctld
# validates and advances with its own localtime_r(). The tests below therefore
# assume the client and the controller share a timezone, which holds for a
# single-host testsuite run.
DAY_INDEX = {
    "Monday": 0,
    "Tuesday": 1,
    "Wednesday": 2,
    "Thursday": 3,
    "Friday": 4,
    "Saturday": 5,
    "Sunday": 6,
}

# slurm_perror() appends strerror(errno) to its own prefix. That string is
# shared by every ESLURM_INVALID_TIME_VALUE rejection, so it says the request
# was refused but not why.
INVALID_TIME = "Invalid time specified"

# The message the controller builds does say why. slurm_create_reservation()
# and slurm_update_reservation() discard it, but the client prints it to
# stderr first: _unpack_return_code2_msg() logs it at LOG_LEVEL_ERROR when it
# unpacks RESPONSE_SLURM_RC_MSG. Assert on it so the tests below cannot pass
# on an unrelated time error.
REJECT_MSG = {
    "WEEKDAY": "A WEEKDAY reservation cannot start on a weekend day",
    "WEEKEND": "A WEEKEND reservation cannot start on a weekday",
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_nodes(2)
    atf.require_slurm_running()


def next_date(day_name):
    """Return the next date falling on day_name.

    The date returned is always in the future, so the tests below do not
    depend on the day or the time at which they run.
    """
    today = datetime.date.today()
    days_ahead = (DAY_INDEX[day_name] - today.weekday()) % 7 or 7

    return today + datetime.timedelta(days=days_ahead)


def format_start(date, time_of_day="10:00:00"):
    """Return date as a reservation start time string."""

    return f"{date.strftime('%Y-%m-%d')}T{time_of_day}"


def next_day(day_name):
    """Return a start time string on the next date falling on day_name."""

    return format_start(next_date(day_name))


def recent_past(minutes):
    """Return the local time the given number of minutes ago.

    Computed from the epoch rather than by subtracting from the wall clock, so
    a DST change in between does not shift it. The start time is still sent as
    local wall-clock time, which is ambiguous in the hour a DST change repeats,
    and Slurm resolves it with mktime(). Skip when a DST change is close enough
    to make that matter.
    """
    now = time.time()
    start = now - minutes * 60
    if time.localtime(start - 3600).tm_isdst != time.localtime(now + 3600).tm_isdst:
        pytest.skip("A DST change near the start time makes it ambiguous")

    return datetime.datetime.fromtimestamp(start)


@pytest.fixture(scope="module", autouse=False)
def node_list():
    return atf.run_job_nodes("-N2 true")


@pytest.fixture(scope="function")
def create_resv(request, node_list):
    global node1, node2
    node1, node2 = node_list
    flag = request.param

    atf.run_command(
        "scontrol create reservation reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now duration=1 nodecnt=1 "
        f"flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    if re.search(
        rf"(?:Nodes=)({node2})", atf.run_command_output("scontrol show res resv1")
    ):
        node_list.reverse()
        node1, node2 = node_list

    return [node1, node2, flag]


@pytest.fixture(scope="function")
def delete_resv():
    yield

    atf.run_command(
        f"scontrol update nodename={node1},{node2} state=idle reason='testing'",
        user=atf.properties["slurm-user"],
    )

    atf.run_command(
        "scontrol delete reservation resv1", user=atf.properties["slurm-user"]
    )


@pytest.fixture(scope="function")
def cleanup_resv():
    """Delete resv1 without touching node states.

    The tests using this fixture never set a node down, so the node reset done
    by delete_resv is not needed. Those tests also do not use create_resv, so
    the node1 and node2 globals it relies on may still be unset.
    """

    yield

    # The tests that expect a rejection never create resv1, so only delete it
    # when it is there. Deleting unconditionally means a real cleanup failure
    # looks the same as the expected "no such reservation", and a leaked resv1
    # then fails the next parametrization with "Duplicate reservation name".
    if "resv1" in atf.get_reservations(quiet=True, fatal=True):
        atf.run_command(
            "scontrol delete reservation resv1",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


@pytest.mark.parametrize("create_resv", ["REPLACE_DOWN", "REPLACE", ""], indirect=True)
def test_replace_flags(create_resv, delete_resv):
    """Verify that nodes in a reservation with flags replace and replace_down
    are replaced when they go down
    """

    node1, node2, flag = create_resv

    logging.info(f"Assert that resv1 initially has {node1}")
    assert (
        re.search(
            rf"(?:Nodes=)({node1})",
            atf.run_command_output("scontrol show reservationname resv1"),
        )
        is not None
    )

    logging.info(f"Assert that {node1} has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: re.search(r"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )

    logging.info(f"Set {node1} down")
    atf.run_command(
        f"scontrol update nodeName={node1} state=down reason='testing'",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert that resv1 now has {node2}")
    assert atf.repeat_command_until(
        "scontrol show reservationname resv1",
        lambda results: re.search(rf"(?:Nodes=)({node2})", results["stdout"]),
        quiet=False,
    )

    logging.info(f"Assert that {node2} now has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node2}",
        lambda results: re.search(r"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )

    logging.info(f"Assert that {node1} has NOT the +RESERVED state anymore")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: not re.search(r"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )


@pytest.mark.parametrize("create_resv", ["STATIC_ALLOC", "MAINT"], indirect=True)
def test_noreplace_flags(create_resv, delete_resv):
    """Verify that nodes in a reservation with static allocations aren't
    replaced when they go down
    """

    node1, node2, flag = create_resv

    logging.info(f"Assert that resv1 initially has {node1}")
    assert (
        re.search(
            rf"(?:Nodes=)({node1})",
            atf.run_command_output("scontrol show reservationname resv1"),
        )
        is not None
    )

    logging.info(f"Assert that {node1} has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: re.search(r"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )

    if flag == "MAINT":
        logging.info(f"Assert that {node1} has the +MAINT state")
        assert atf.repeat_command_until(
            f"scontrol show node {node1}",
            lambda results: re.search(r"(?:State=).+(\+MAINT)", results["stdout"]),
            quiet=False,
        )

    logging.info(f"Set {node1} down")
    atf.run_command(
        f"scontrol update nodeName={node1} state=down reason='testing'",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert that resv1 won't have {node2}")
    assert not atf.repeat_command_until(
        "scontrol show reservationname resv1",
        lambda results: re.search(rf"(?:Nodes=)({node2})", results["stdout"]),
        timeout=15,
        quiet=False,
    )

    logging.info(f"Assert that {node2} doesn't get the +RESERVED state")
    assert not atf.repeat_command_until(
        f"scontrol show node {node2}",
        lambda results: re.search(r"(?:State=).+(\+RESERVED)", results["stdout"]),
        timeout=15,
        quiet=False,
    )

    if flag == "MAINT":
        logging.info(f"Assert that {node1} still has the +MAINT state")
        assert atf.repeat_command_until(
            f"scontrol show node {node1}",
            lambda results: re.search(r"(?:State=).+(\+MAINT)", results["stdout"]),
            quiet=False,
        )


@pytest.mark.parametrize(
    "flag",
    [
        "MAINT,REPLACE",
        "STATIC_ALLOC,REPLACE",
        "MAINT,REPLACE_DOWN",
        "STATIC_ALLOC,REPLACE_DOWN",
        "REPLACE,MAINT",
        "REPLACE_DOWN,MAINT",
        "REPLACE,STATIC_ALLOC",
        "REPLACE_DOWN,STATIC_ALLOC",
        "HOURLY,WEEKDAY",
        "WEEKDAY,WEEKEND",
        "WEEKEND,WEEKLY",
        "WEEKLY,HOURLY",
        "TIME_FLOAT,HOURLY",
        "TIME_FLOAT,WEEKDAY",
        "TIME_FLOAT,WEEKEND",
        "TIME_FLOAT,WEEKLY",
        "HOURLY,TIME_FLOAT",
        "WEEKDAY,TIME_FLOAT",
        "WEEKEND,TIME_FLOAT",
        "WEEKLY,TIME_FLOAT",
    ],
)
def test_incomp_flags(flag, cleanup_resv):
    """Verify that reservations are not allowed to be created with incompatible
    flags"""

    logging.info(f"Attempt to create a reservation with incompatible flags: {flag}")
    result = atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now duration=1 nodecnt=1 "
        f"flags={flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = "Error creating the reservation"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), f"Could not find {expected_output} in {result['stderr']}"

    logging.info("Assert exit code is not 0")
    assert result["exit_code"] != 0, "The command was supposed to fail, but didn't!"


@pytest.mark.parametrize(
    "create_flag, update_flag",
    [
        ("MAINT", "REPLACE"),
        ("STATIC_ALLOC", "REPLACE"),
        ("MAINT", "REPLACE_DOWN"),
        ("STATIC_ALLOC", "REPLACE_DOWN"),
        ("REPLACE", "MAINT"),
        ("REPLACE_DOWN", "MAINT"),
        ("REPLACE", "STATIC_ALLOC"),
        ("REPLACE_DOWN", "STATIC_ALLOC"),
        ("HOURLY", "WEEKDAY"),
        ("WEEKDAY", "WEEKEND"),
        ("WEEKEND", "WEEKLY"),
        ("WEEKLY", "HOURLY"),
        ("TIME_FLOAT", "HOURLY"),
        ("TIME_FLOAT", "WEEKDAY"),
        ("TIME_FLOAT", "WEEKEND"),
        ("TIME_FLOAT", "WEEKLY"),
        ("HOURLY", "TIME_FLOAT"),
        ("WEEKDAY", "TIME_FLOAT"),
        ("WEEKEND", "TIME_FLOAT"),
        ("WEEKLY", "TIME_FLOAT"),
    ],
)
def test_update_incomp_flags(create_flag, update_flag, cleanup_resv):
    """Verify that reservations created with a a given flag cannot be
    updated with incompatible flags"""

    # WEEKDAY and WEEKEND reservations must start on a day the flag covers, so
    # "now" cannot be used for them. Otherwise the creation below fails on the
    # wrong days and the update is left with no reservation to act on.
    if create_flag == "WEEKDAY":
        start = next_day("Monday")
    elif create_flag == "WEEKEND":
        start = next_day("Saturday")
    else:
        start = "now"

    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={start} duration=1 nodecnt=1 "
        f"flags={create_flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    result = atf.run_command(
        f"scontrol update reservation=resv1 flags={update_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = "slurm_update error"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), "Could not find 'error updating the res' in output"

    logging.info("Assert exit code is not 0")
    assert result["exit_code"] != 0, "The command was supposed to fail, but didn't!"


@pytest.mark.xfail(
    CTLD_VERSION < (26, 11),
    reason="Ticket 17310: WEEKDAY/WEEKEND start day validation added in 26.11",
)
@pytest.mark.parametrize(
    "flag, day",
    [
        ("WEEKDAY", "Saturday"),
        ("WEEKDAY", "Sunday"),
        ("WEEKEND", "Monday"),
        ("WEEKEND", "Friday"),
    ],
)
def test_create_reoccurring_bad_start_day(flag, day, cleanup_resv):
    """Verify that a WEEKDAY or WEEKEND reservation cannot be created with a
    start time outside of the days covered by the flag"""

    logging.info(f"Attempt to create a {flag} reservation starting on a {day}")
    result = atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = f"Error creating the reservation: {INVALID_TIME}"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), f"Could not find {expected_output} in {result['stderr']}"

    logging.info(f"Assert output message contains {REJECT_MSG[flag]}")
    assert (
        REJECT_MSG[flag] in result["stderr"]
    ), f"Could not find {REJECT_MSG[flag]} in {result['stderr']}"

    logging.info("Assert the reservation was not created")
    assert "resv1" not in atf.get_reservations(
        fatal=True
    ), "Reservation resv1 should not exist after a rejected creation"


@pytest.mark.xfail(
    CTLD_VERSION < (26, 11),
    reason="Ticket 17310: WEEKDAY/WEEKEND start day validation added in 26.11",
)
def test_create_reoccurring_start_now_bad_day(cleanup_resv):
    """Verify that start=now gets no exemption from the start day rule. The
    flag to test is whichever one today does not satisfy."""

    flag = "WEEKEND" if datetime.date.today().weekday() < 5 else "WEEKDAY"

    logging.info(f"Attempt to create a {flag} reservation starting now")
    result = atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    logging.info(f"Assert output message contains {REJECT_MSG[flag]}")
    assert (
        REJECT_MSG[flag] in result["stderr"]
    ), f"Could not find {REJECT_MSG[flag]} in {result['stderr']}"

    logging.info("Assert the reservation was not created")
    assert "resv1" not in atf.get_reservations(
        fatal=True
    ), "Reservation resv1 should not exist after a rejected creation"


@pytest.mark.parametrize(
    "flag, day",
    [
        ("WEEKDAY", "Monday"),
        ("WEEKDAY", "Friday"),
        ("WEEKEND", "Saturday"),
        ("WEEKEND", "Sunday"),
    ],
)
def test_create_reoccurring_good_start_day(flag, day, cleanup_resv):
    """Verify that a WEEKDAY or WEEKEND reservation can be created with a start
    time inside of the days covered by the flag"""

    logging.info(f"Create a {flag} reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation has the {flag} flag")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag in flags, f"Reservation resv1 should have the {flag} flag, got {flags}"


@pytest.mark.xfail(
    CTLD_VERSION < (26, 11),
    reason="Ticket 17310: WEEKDAY/WEEKEND start day validation added in 26.11",
)
@pytest.mark.parametrize(
    "flag, day",
    [
        ("WEEKDAY", "Sunday"),
        ("WEEKEND", "Friday"),
    ],
)
def test_create_reoccurring_bad_start_day_over_midnight(flag, day, cleanup_resv):
    """Verify that the start day is rejected even when the occurrence ends on
    a day the flag does cover"""

    logging.info(f"Attempt to create a {flag} reservation starting on a {day}")
    result = atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} "
        f"start={format_start(next_date(day), '23:00:00')} "
        f"duration=240 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    logging.info(f"Assert output message contains {REJECT_MSG[flag]}")
    assert (
        REJECT_MSG[flag] in result["stderr"]
    ), f"Could not find {REJECT_MSG[flag]} in {result['stderr']}"

    logging.info("Assert the reservation was not created")
    assert "resv1" not in atf.get_reservations(
        fatal=True
    ), "Reservation resv1 should not exist after a rejected creation"


@pytest.mark.parametrize(
    "flag, day",
    [
        ("WEEKDAY", "Friday"),
        ("WEEKEND", "Sunday"),
    ],
)
def test_create_reoccurring_good_start_day_over_midnight(flag, day, cleanup_resv):
    """Verify that an occurrence may run past midnight into a day the flag
    does not cover, as long as it starts on one that it does"""

    start_date = next_date(day)

    logging.info(f"Create a {flag} reservation starting on a {day} at 23:00")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} "
        f"start={format_start(start_date, '23:00:00')} "
        f"duration=240 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    expected_end = format_start(start_date + datetime.timedelta(days=1), "03:00:00")

    logging.info(f"Assert the reservation ends at {expected_end}")
    end_time = atf.get_reservation_parameter("resv1", "EndTime", default="")
    assert (
        end_time == expected_end
    ), f"Reservation resv1 should end at {expected_end}, got EndTime={end_time}"


@pytest.mark.xfail(
    CTLD_VERSION < (26, 11),
    reason="Ticket 17310: WEEKDAY/WEEKEND start day validation added in 26.11",
)
@pytest.mark.parametrize(
    "day, flag, syntax",
    [
        ("Saturday", "WEEKDAY", "flags="),
        ("Sunday", "WEEKDAY", "flags="),
        ("Monday", "WEEKEND", "flags="),
        ("Friday", "WEEKEND", "flags="),
        # scontrol preprocesses flags+= into the same request that flags=
        # builds, so one case covers the client wrapper.
        ("Saturday", "WEEKDAY", "flags+="),
    ],
)
def test_update_reoccurring_bad_flag(day, flag, syntax, cleanup_resv):
    """Verify that a reservation cannot be updated with a WEEKDAY or WEEKEND
    flag that does not cover its start day"""

    logging.info(f"Create a reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        "duration=1 nodecnt=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Attempt to add the {flag} flag to the reservation")
    result = atf.run_command(
        f"scontrol update reservation=resv1 {syntax}{flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = f"slurm_update error: {INVALID_TIME}"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), f"Could not find {expected_output} in {result['stderr']}"

    logging.info(f"Assert output message contains {REJECT_MSG[flag]}")
    assert (
        REJECT_MSG[flag] in result["stderr"]
    ), f"Could not find {REJECT_MSG[flag]} in {result['stderr']}"

    # update_resv() sets the flag on the live record before it validates, so
    # the rejection is only correct if update_failure restores the backup.
    logging.info(f"Assert the reservation did not keep the {flag} flag")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag not in flags, f"Reservation resv1 should not have {flag}, got {flags}"


@pytest.mark.xfail(
    CTLD_VERSION < (26, 11),
    reason="Ticket 17310: WEEKDAY/WEEKEND start day validation added in 26.11",
)
@pytest.mark.parametrize(
    "flag, day, bad_day",
    [
        ("WEEKDAY", "Monday", "Saturday"),
        ("WEEKDAY", "Monday", "Sunday"),
        ("WEEKEND", "Saturday", "Monday"),
        ("WEEKEND", "Sunday", "Friday"),
    ],
)
def test_update_reoccurring_bad_start_day(flag, day, bad_day, cleanup_resv):
    """Verify that a WEEKDAY or WEEKEND reservation cannot be updated with a
    start time outside of the days covered by the flag"""

    start = next_day(day)

    logging.info(f"Create a {flag} reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={start} "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Attempt to move the reservation start time to a {bad_day}")
    result = atf.run_command(
        f"scontrol update reservation=resv1 starttime={next_day(bad_day)}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = f"slurm_update error: {INVALID_TIME}"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), f"Could not find {expected_output} in {result['stderr']}"

    logging.info(f"Assert output message contains {REJECT_MSG[flag]}")
    assert (
        REJECT_MSG[flag] in result["stderr"]
    ), f"Could not find {REJECT_MSG[flag]} in {result['stderr']}"

    # update_resv() moves start_time on the live record before it validates,
    # so the rejection is only correct if update_failure restores the backup.
    logging.info(f"Assert the reservation still starts on the {day}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == start
    ), f"Reservation resv1 should still start at {start}, got StartTime={start_time}"


@pytest.mark.parametrize(
    "flag, day, new_day",
    [
        ("WEEKDAY", "Monday", "Friday"),
        ("WEEKEND", "Saturday", "Sunday"),
    ],
)
def test_update_reoccurring_good_start_day(flag, day, new_day, cleanup_resv):
    """Verify that moving a WEEKDAY or WEEKEND reservation to another day its
    flag covers is still accepted. The tests above only cover the updates the
    start day rule rejects, so this one guards it against rejecting too much."""

    logging.info(f"Create a {flag} reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    new_start_date = next_date(new_day)
    new_start = format_start(new_start_date)

    logging.info(f"Move the reservation start time to a {new_day}")
    atf.run_command(
        f"scontrol update reservation=resv1 starttime={new_start}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation now starts at {new_start}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == new_start
    ), f"Reservation resv1 should start at {new_start}, got StartTime={start_time}"

    # end_time is recomputed separately from start_time, so an update that
    # moves one and not the other silently resizes the reservation.
    expected_end = format_start(new_start_date, "10:01:00")

    logging.info(f"Assert the reservation now ends at {expected_end}")
    end_time = atf.get_reservation_parameter("resv1", "EndTime", default="")
    assert (
        end_time == expected_end
    ), f"Reservation resv1 should end at {expected_end}, got EndTime={end_time}"


@pytest.mark.parametrize(
    "flag, day",
    [
        ("WEEKDAY", "Monday"),
        ("WEEKEND", "Saturday"),
    ],
)
def test_update_reoccurring_good_flag(flag, day, cleanup_resv):
    """Verify that adding a WEEKDAY or WEEKEND flag to a reservation that
    already starts on a day the flag covers is accepted"""

    logging.info(f"Create a reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        "duration=1 nodecnt=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Add the {flag} flag to the reservation")
    atf.run_command(
        f"scontrol update reservation=resv1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation has the {flag} flag")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag in flags, f"Reservation resv1 should have the {flag} flag, got {flags}"


@pytest.mark.xfail(
    CTLD_VERSION < (26, 11),
    reason="Ticket 17310: WEEKDAY/WEEKEND start day validation added in 26.11",
)
@pytest.mark.parametrize(
    "old_flag, day, new_flag, new_day",
    [
        ("WEEKDAY", "Monday", "WEEKEND", "Saturday"),
        ("WEEKEND", "Saturday", "WEEKDAY", "Monday"),
    ],
)
def test_update_reoccurring_swap_flag(old_flag, day, new_flag, new_day, cleanup_resv):
    """Verify the documented way to replace a reoccurring flag.

    scontrol.1 documents the Flags=-OLD,+NEW form as the way to remove one
    flag and add another in the same request. Between WEEKDAY and WEEKEND that
    alone is always rejected, because the existing start day is never covered
    by the incoming flag, so reservations.shtml pairs the swap with a new
    StartTime on a day that flag covers.
    """

    logging.info(f"Create a {old_flag} reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        f"duration=1 nodecnt=1 flags={old_flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Attempt to swap {old_flag} for {new_flag} on its own")
    result = atf.run_command(
        f"scontrol update reservation=resv1 flags=-{old_flag},+{new_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    logging.info(f"Assert output message contains {REJECT_MSG[new_flag]}")
    assert (
        REJECT_MSG[new_flag] in result["stderr"]
    ), f"Could not find {REJECT_MSG[new_flag]} in {result['stderr']}"

    logging.info(f"Assert the reservation still has {old_flag}")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert (
        old_flag in flags
    ), f"Reservation resv1 should still have {old_flag}, got {flags}"

    new_start = next_day(new_day)

    logging.info(f"Swap the flag again, this time with a {new_day} start time")
    atf.run_command(
        f"scontrol update reservation=resv1 "
        f"flags=-{old_flag},+{new_flag} starttime={new_start}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation now has {new_flag} and not {old_flag}")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert new_flag in flags, f"Reservation resv1 should have {new_flag}, got {flags}"
    assert (
        old_flag not in flags
    ), f"Reservation resv1 should not have {old_flag}, got {flags}"

    logging.info(f"Assert the reservation now starts at {new_start}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == new_start
    ), f"Reservation resv1 should start at {new_start}, got StartTime={start_time}"


@pytest.mark.parametrize(
    "old_flag, new_flag",
    [
        ("DAILY", "WEEKLY"),
        ("WEEKLY", "DAILY"),
    ],
)
def test_update_reoccurring_swap_unconstrained_flag(old_flag, new_flag, cleanup_resv):
    """Verify the documented way to replace a reoccurring flag that does not
    constrain the start day.

    scontrol.1 documents the Flags=-OLD,+NEW form as the way to remove one
    flag and add another in the same request. Neither DAILY nor WEEKLY
    restricts which day the reservation starts on, so unlike the
    WEEKDAY/WEEKEND swap this command is accepted on its own, and it has to
    leave the start time where it was.
    """

    # Any future start suits both flags. Resolve it once so a local date
    # rollover cannot make the create and the assertion disagree.
    start = format_start(datetime.date.today() + datetime.timedelta(days=1))

    logging.info(f"Create a {old_flag} reservation")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={start} "
        f"duration=1 nodecnt=1 flags={old_flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Swap {old_flag} for {new_flag} without giving a start time")
    atf.run_command(
        f"scontrol update reservation=resv1 flags=-{old_flag},+{new_flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation now has {new_flag} and not {old_flag}")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert new_flag in flags, f"Reservation resv1 should have {new_flag}, got {flags}"
    assert (
        old_flag not in flags
    ), f"Reservation resv1 should not have {old_flag}, got {flags}"

    logging.info(f"Assert the reservation still starts at {start}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == start
    ), f"Reservation resv1 should still start at {start}, got StartTime={start_time}"


@pytest.mark.parametrize(
    "flag, day, freed_day",
    [
        ("WEEKDAY", "Monday", "Saturday"),
        ("WEEKEND", "Saturday", "Monday"),
    ],
)
def test_update_reoccurring_remove_flag(flag, day, freed_day, cleanup_resv):
    """Verify that removing a reoccurring flag is accepted, and that the
    start day it constrained is then free to move"""

    logging.info(f"Create a {flag} reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Remove the {flag} flag with the flags-= form")
    atf.run_command(
        f"scontrol update reservation=resv1 flags-={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation no longer has the {flag} flag")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag not in flags, f"Reservation resv1 should not have {flag}, got {flags}"

    freed_start = next_day(freed_day)

    logging.info(f"Move the start time to a {freed_day}, which {flag} forbade")
    atf.run_command(
        f"scontrol update reservation=resv1 starttime={freed_start}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation now starts at {freed_start}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == freed_start
    ), f"Reservation resv1 should start at {freed_start}, got {start_time}"


@pytest.mark.parametrize(
    "flag, day, other_flag",
    [
        ("WEEKDAY", "Monday", "WEEKEND"),
        ("WEEKEND", "Saturday", "WEEKDAY"),
    ],
)
def test_update_reoccurring_flag_inherits_sign(flag, day, other_flag, cleanup_resv):
    """Verify that a flag given without a sign inherits the sign of the flag
    before it, so Flags=-WEEKDAY,WEEKEND removes both flags instead of
    swapping them, as scontrol.1 warns"""

    logging.info(f"Create a {flag} reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={next_day(day)} "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Update the flags with flags=-{flag},{other_flag}")
    atf.run_command(
        f"scontrol update reservation=resv1 flags=-{flag},{other_flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation has neither {flag} nor {other_flag}")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag not in flags, f"Reservation resv1 should not have {flag}, got {flags}"
    assert (
        other_flag not in flags
    ), f"Reservation resv1 should not have {other_flag}, got {flags}"


@pytest.mark.parametrize(
    "flag, day",
    [
        ("WEEKDAY", "Monday"),
        ("WEEKEND", "Saturday"),
    ],
)
def test_update_reoccurring_unrelated_field(flag, day, cleanup_resv):
    """Verify that an update which sets neither the start time nor a
    reoccurring flag is accepted, and leaves both of them alone"""

    start = next_day(day)

    logging.info(f"Create a {flag} reservation starting on a {day}")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={start} "
        f"duration=1 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info("Update the duration only")
    atf.run_command(
        "scontrol update reservation=resv1 duration=120",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation still starts at {start}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == start
    ), f"Reservation resv1 should still start at {start}, got StartTime={start_time}"

    logging.info(f"Assert the reservation still has the {flag} flag")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag in flags, f"Reservation resv1 should still have {flag}, got {flags}"


@pytest.mark.parametrize("swap", [False, True], ids=["starttime", "swap"])
def test_update_active_reoccurring_start_day(swap, cleanup_resv):
    """Verify that the start time of an active reservation cannot be moved,
    on its own or together with a reoccurring flag swap.

    scontrol.1 documents the restriction, and reservations.shtml leans on it
    when it says a WEEKDAY/WEEKEND swap cannot be done while the reservation
    is active: that swap needs a new start time in the same command, which an
    active reservation refuses.
    """

    # Started already, so the reservation is ACTIVE, and within
    # MAX_RESV_DELAY, so the create does not need FORCE_START. The rule is
    # checked against that start, so the flag to create with is whichever one
    # the start day satisfies.
    start = recent_past(2)
    started = start.strftime("%Y-%m-%dT%H:%M:%S")
    flag = "WEEKDAY" if start.weekday() < 5 else "WEEKEND"
    new_flag = "WEEKEND" if flag == "WEEKDAY" else "WEEKDAY"

    # The new start day is one the flag the reservation ends up with covers,
    # so the start day rule has no reason to reject either command and only
    # the active reservation can.
    covered_day = {"WEEKDAY": "Monday", "WEEKEND": "Saturday"}
    new_start = next_day(covered_day[new_flag if swap else flag])

    logging.info(f"Create a {flag} reservation that started 2 minutes ago")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={started} "
        f"duration=30 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info("Assert the reservation is active")
    state = atf.get_reservation_parameter("resv1", "State", default="")
    assert state == "ACTIVE", f"Reservation resv1 should be ACTIVE, got State={state}"

    update = f"starttime={new_start}"
    if swap:
        update = f"flags=-{flag},+{new_flag} {update}"

    logging.info(f"Attempt to update the active reservation with {update}")
    result = atf.run_command(
        f"scontrol update reservation=resv1 {update}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = "slurm_update error: Reservation already started"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), f"Could not find {expected_output} in {result['stderr']}"

    logging.info(f"Assert the reservation still starts at {started}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == started
    ), f"Reservation resv1 should still start at {started}, got StartTime={start_time}"

    # update_resv() sets the incoming flags on the live record before it
    # validates the start time, so the swap case is only correct if
    # update_failure restores the backup.
    logging.info(f"Assert the reservation still has {flag} and not {new_flag}")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag in flags, f"Reservation resv1 should still have {flag}, got {flags}"
    assert (
        new_flag not in flags
    ), f"Reservation resv1 should not have {new_flag}, got {flags}"


def test_update_reoccurring_swap_after_skip(cleanup_resv):
    """Verify that a WEEKDAY/WEEKEND swap refused while the reservation is
    active is accepted once it has moved to its next occurrence.

    reservations.shtml says the swap is accepted again once the occurrence
    ends. Skipping the occurrence advances the reservation the same way the
    end of the occurrence does, without waiting for it.
    """

    # Started already, so the reservation is ACTIVE; see
    # test_update_active_reoccurring_start_day.
    start = recent_past(2)
    flag = "WEEKDAY" if start.weekday() < 5 else "WEEKEND"
    new_flag = "WEEKEND" if flag == "WEEKDAY" else "WEEKDAY"
    new_start = next_day({"WEEKDAY": "Monday", "WEEKEND": "Saturday"}[new_flag])

    logging.info(f"Create a {flag} reservation that started 2 minutes ago")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} "
        f"start={start.strftime('%Y-%m-%dT%H:%M:%S')} "
        f"duration=30 nodecnt=1 flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info("Skip the current occurrence to make the reservation advance")
    atf.run_command(
        "scontrol update reservation=resv1 skip",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info("Assert the reservation is no longer active")
    state = atf.get_reservation_parameter("resv1", "State", default="")
    assert (
        state == "INACTIVE"
    ), f"Reservation resv1 should be INACTIVE, got State={state}"

    logging.info(f"Swap {flag} for {new_flag} with a start time it covers")
    atf.run_command(
        f"scontrol update reservation=resv1 "
        f"flags=-{flag},+{new_flag} starttime={new_start}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation now has {new_flag} and not {flag}")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert new_flag in flags, f"Reservation resv1 should have {new_flag}, got {flags}"
    assert flag not in flags, f"Reservation resv1 should not have {flag}, got {flags}"

    logging.info(f"Assert the reservation now starts at {new_start}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == new_start
    ), f"Reservation resv1 should start at {new_start}, got StartTime={start_time}"


@pytest.mark.xfail(
    CTLD_VERSION < (26, 11),
    reason="Ticket 17310: WEEKDAY/WEEKEND start day validation added in 26.11",
)
def test_create_reoccurring_force_start_bad_day(cleanup_resv):
    """Verify that FORCE_START does not waive the start day rule.

    FORCE_START only does anything for a start time in the past, so the
    reservation starts within the last 20 minutes, which near midnight is the
    previous day. The rule is checked against that start, so the flag to test
    is whichever one the start day does not satisfy.
    """

    # Start before MAX_RESV_DELAY so the create is only accepted because of
    # FORCE_START, but end after it so FORCE_START itself still accepts it.
    start = recent_past(20)
    flag = "WEEKEND" if start.weekday() < 5 else "WEEKDAY"

    logging.info(f"Attempt to create a {flag},FORCE_START reservation started today")
    result = atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} "
        f"start={start.strftime('%Y-%m-%dT%H:%M:%S')} "
        f"duration=25 nodecnt=1 flags={flag},FORCE_START",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    logging.info(f"Assert output message contains {REJECT_MSG[flag]}")
    assert (
        REJECT_MSG[flag] in result["stderr"]
    ), f"Could not find {REJECT_MSG[flag]} in {result['stderr']}"

    logging.info("Assert the reservation was not created")
    assert "resv1" not in atf.get_reservations(
        fatal=True
    ), "Reservation resv1 should not exist after a rejected creation"


def test_create_reoccurring_force_start_good_day(cleanup_resv):
    """Verify that FORCE_START keeps a past start time on a day the flag
    covers.

    The complement of test_create_reoccurring_force_start_bad_day: here the
    flag under test is the one the start day does satisfy, so the start day
    rule has nothing to say and FORCE_START is left to accept the past start
    time on its own.
    """

    # Start before MAX_RESV_DELAY so the create is only accepted because of
    # FORCE_START, but end after it so FORCE_START itself still accepts it.
    start = recent_past(20)
    flag = "WEEKDAY" if start.weekday() < 5 else "WEEKEND"
    expected = start.strftime("%Y-%m-%dT%H:%M:%S")

    logging.info(f"Create a {flag},FORCE_START reservation started 20 minutes ago")
    atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start={expected} "
        f"duration=25 nodecnt=1 flags={flag},FORCE_START",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert the reservation kept the past start time {expected}")
    start_time = atf.get_reservation_parameter("resv1", "StartTime", default="")
    assert (
        start_time == expected
    ), f"Reservation resv1 should start at {expected}, got StartTime={start_time}"

    logging.info(f"Assert the reservation has the {flag} flag")
    flags = atf.get_reservation_parameter("resv1", "Flags", default="") or ""
    assert flag in flags, f"Reservation resv1 should have the {flag} flag, got {flags}"
