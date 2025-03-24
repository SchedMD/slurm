############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf
import datetime
import logging


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(4)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def get_and_down_nodes():
    logging.info(f"Getting the necessary 3 nodes:")
    nodes = atf.run_job_nodes("-N3 true", fatal=True)

    logging.info(f"Setting ALL nodes Down:")
    atf.run_command(
        "scontrol update nodename=ALL state=down reason=test_resv_overlap",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.run_command_output(
            "sinfo --states=idle,mixed --Format=NodeList -h"
        ),
        lambda not_down: not_down == "",
        fatal=True,
    )

    yield nodes

    atf.run_command(
        "scontrol delete reservationname=resv1",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        "scontrol delete reservationname=resv2",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        "scontrol delete reservationname=resv3",
        user=atf.properties["slurm-user"],
        quiet=True,
    )

    atf.run_command(
        "scontrol update nodename=ALL state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.mark.parametrize("reocurring_flag", ["HOURLY", "DAILY", "WEEKLY"])
def test_overlap_weeks(request, get_and_down_nodes, reocurring_flag):
    """Verify that reservations don't overlap nodes if they are have a start time difference greater than a week"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(f"Creating resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv1 StartTime=NOW+3weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv2 can be created before resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime=NOW+1week nodecnt=1 duration=00:15:00 user={atf.properties['test-user']}",
        user=atf.properties["slurm-user"],
    )
    assert (
        rc["exit_code"] == 0
    ), f"resv2 should be created before resv1 because {nodes[0]} should is not yet reserved by resv1"

    logging.info(f"Ensuring that resv3 cannot be created after resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv3 StartTime=NOW+5weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv3 should NOT be created after resv1 because {nodes[0]} should be used only by resv1"

    logging.info(
        f"Double-checking that that there is only 2 reservations with the expected node:"
    )
    assert (
        len(atf.get_reservations()) == 2
    ), f"only resv1 and resv2 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"
    assert (
        atf.get_reservation_parameter("resv2", "Nodes") == nodes[0]
    ), f"resv2 should use node {nodes[0]}"


@pytest.mark.parametrize("reocurring_flag", ["HOURLY", "DAILY", "WEEKLY"])
def test_overlap_weeks_reverse(request, get_and_down_nodes, reocurring_flag):
    """Verify that reocurring reservations don't overlap nodes if they are have a start time difference greater than a week"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(f"Creating resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv1 StartTime=NOW+3weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']}",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv2 cannot be created before resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime=NOW+1week nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv2 should NOT be created before resv1 because {nodes[0]} should be reserved by resv1"

    logging.info(f"Ensuring that resv3 can be created after resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv3 StartTime=NOW+5weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
    )
    assert (
        rc["exit_code"] == 0
    ), f"resv3 should be created after resv1 because {nodes[0]} should is not anymore reserved by resv1"

    logging.info(
        f"Double-checking that that there is only 2 reservations with the expected node:"
    )
    assert (
        len(atf.get_reservations()) == 2
    ), f"only resv1 and resv2 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"
    assert (
        atf.get_reservation_parameter("resv3", "Nodes") == nodes[0]
    ), f"resv3 should use node {nodes[0]}"


@pytest.mark.parametrize("reocurring_flag", ["HOURLY", "DAILY", "WEEKLY"])
def test_overlap_reocurring(request, get_and_down_nodes, reocurring_flag):
    """Verify that reocurring reservations don't overlap when the start at 1 reocurring period"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    if reocurring_flag == "HOURLY":
        resv1_start = "2hours"
        resv2_start = "1hour"
        resv3_start = "3hours"
    elif reocurring_flag == "DAILY":
        resv1_start = "2days"
        resv2_start = "1day"
        resv3_start = "3days"
    elif reocurring_flag == "WEEKLY":
        resv1_start = "2weeks"
        resv2_start = "1week"
        resv3_start = "3weeks"

    logging.info(f"Creating resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv1 StartTime=NOW+{resv1_start} nodecnt=1 duration=00:10:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv2 cannot be created before resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime=NOW+{resv2_start} nodecnt=1 duration=00:10:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv2 should NOT be created before resv1 because {nodes[0]} should be used only by resv1"

    logging.info(f"Ensuring that resv3 cannot be created after resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv3 StartTime=NOW+{resv3_start} nodecnt=1 duration=00:10:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv3 should NOT be created after resv1 because {nodes[0]} should be used only by resv1"

    logging.info(
        f"Double-checking that that there is only 1 reservation with the expected node:"
    )
    assert len(atf.get_reservations()) == 1, f"only resv1 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"


@pytest.mark.parametrize("reocurring_flag", ["HOURLY", "DAILY", "WEEKLY"])
def test_overlap_reocurring_week(request, get_and_down_nodes, reocurring_flag):
    """Verify that reocurring reservations don't overlap if they are have a start time difference greater than a week"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(f"Creating resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv1 StartTime=NOW+3weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv2 cannot be created before resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime=NOW+1weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv2 should NOT be created before resv1 because {nodes[0]} should be used only by resv1"

    logging.info(f"Ensuring that resv3 cannot be created after resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv3 StartTime=NOW+5weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv3 should NOT be created after resv1 because {nodes[0]} should be used only by resv1"

    logging.info(
        f"Double-checking that that there is only 1 reservation with the expected node:"
    )
    assert len(atf.get_reservations()) == 1, f"only resv1 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"


@pytest.mark.parametrize("reocurring_flag", ["HOURLY", "DAILY", "WEEKLY"])
def test_overlap_replacing(request, get_and_down_nodes, reocurring_flag):
    """Verify that reservations don't overlap when replacing nodes"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(f"Creating resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv1 StartTime=NOW+1minutes nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag},REPLACE_DOWN,PURGE_COMP=1",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv2 cannot be created yet:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime=NOW+1minutes nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag},REPLACE_DOWN,PURGE_COMP=3",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv2 should NOT be created because {nodes[0]} should be used only by resv1"

    logging.info(
        f"Double-checking that that there is only 1 reservation with the expected node:"
    )
    assert len(atf.get_reservations()) == 1, f"only resv1 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"

    logging.info(f"Resuming node {nodes[1]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[1]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[1], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(f"Creating resv2:")
    rc2 = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime=NOW+1minutes nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag},REPLACE_DOWN,PURGE_COMP=3",
        user=atf.properties["slurm-user"],
    )
    assert rc2["exit_code"] == 0, f"resv2 should be created"

    logging.info(
        f"Double-checking that there are only 2 reservation and they are using different nodes:"
    )
    assert len(atf.get_reservations()) == 2, f"resv1 and resv2 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"
    assert (
        atf.get_reservation_parameter("resv2", "Nodes") == nodes[1]
    ), f"resv2 should use node {nodes[1]}"

    logging.info(
        f"Waiting until resv1 and resv2 are ACTIVE (should be started in 1 minute)..."
    )
    atf.repeat_until(
        lambda: atf.get_reservation_parameter("resv1", "State"),
        lambda state: state == "ACTIVE",
        fatal=True,
        timeout=90,
        poll_interval=5,
    )
    atf.repeat_until(
        lambda: atf.get_reservation_parameter("resv2", "State"),
        lambda state: state == "ACTIVE",
        fatal=True,
        timeout=90,
        poll_interval=5,
    )

    logging.info(
        f"Setting node {nodes[1]} down, so resv2 won't have any valid node and will try to look for a new one:"
    )
    atf.run_command(
        f"scontrol update nodename={nodes[1]} state=down reason=test_resv_overlap",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[1], "State"),
        lambda state: "DOWN" in state,
        fatal=True,
    )

    logging.info(f"Waiting until resv1 is INACTIVE (should be purged 1 minute)...")
    atf.repeat_until(
        lambda: atf.get_reservation_parameter("resv1", "State"),
        lambda state: state == "INACTIVE",
        fatal=True,
        timeout=165,
        poll_interval=5,
    )

    logging.info(
        f"Ensuring that there resv2 won't steal any node (it will try it every PERIODIC_TIMEOUT=30s):"
    )
    assert not atf.repeat_until(
        lambda: atf.get_reservation_parameter("resv2", "Nodes"),
        lambda rnodes: rnodes != nodes[1],
        timeout=60,
    )

    logging.info(f"Resuming node {nodes[2]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[2]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[2], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(
        f"Checking that resv2 picks the new available node, and that not overlaping reservations happen:"
    )
    assert atf.repeat_until(
        lambda: atf.get_reservation_parameter("resv2", "Nodes"),
        lambda rnodes: rnodes != nodes[1],
        timeout=60,
    )

    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should still use node {nodes[0]}"
    assert (
        atf.get_reservation_parameter("resv2", "Nodes") == nodes[2]
    ), f"resv2 should use node {nodes[2]} now"


@pytest.mark.parametrize(
    "reocurring_flag,week_flag",
    [
        ("HOURLY", "WEEKDAY"),
        ("HOURLY", "WEEKEND"),
        ("DAILY", "WEEKDAY"),
        ("DAILY", "WEEKEND"),
        ("WEEKDAY", "WEEKDAY"),
        ("WEEKEND", "WEEKEND"),
    ],
)
def test_overlap_weekdays(request, get_and_down_nodes, reocurring_flag, week_flag):
    """Verify that reocurring reservations with weekdays and weekends handle overlapping properly with other reocurrings"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(f"Creating resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv1 StartTime=NOW+3weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={reocurring_flag}",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv2 cannot be created before resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime=NOW+1weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={week_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv2 should NOT be created before resv1 because {nodes[0]} should be used only by resv1"

    logging.info(f"Ensuring that resv3 cannot be created after resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv3 StartTime=NOW+5weeks nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={week_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert (
        rc["exit_code"] != 0
    ), f"resv3 should NOT be created after resv1 because {nodes[0]} should be used only by resv1"

    logging.info(
        f"Double-checking that that there is only 1 reservation with the expected node:"
    )
    assert len(atf.get_reservations()) == 1, f"only resv1 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"


@pytest.mark.parametrize(
    "resv1_flag,resv2_flag", [("WEEKEND", "WEEKDAY"), ("WEEKDAY", "WEEKEND")]
)
def test_no_overlap_weekday_weekend(
    request, get_and_down_nodes, resv1_flag, resv2_flag
):
    """Verify that reocurring reservations with weekdays and weekends handle overlapping properly between them"""

    nodes = get_and_down_nodes

    # Current code implementation allows start times not in weekends or
    # weekdays even if reservations used those flags.
    # So, we need to specify the right start times/dates for this test.
    today = datetime.date.today()
    if resv1_flag == "WEEKEND":
        # resv1: next saturday
        # resv2: next thursday
        # resv3: next next thursday
        resv1_start = today + datetime.timedelta(weeks=1, days=5 - today.weekday())
        resv2_start = today + datetime.timedelta(weeks=1, days=3 - today.weekday())
        resv3_start = today + datetime.timedelta(weeks=2, days=3 - today.weekday())
    else:
        # resv1: next next thursday
        # resv2: next saturday
        # resv3: next next saturday
        resv1_start = today + datetime.timedelta(weeks=2, days=3 - today.weekday())
        resv2_start = today + datetime.timedelta(weeks=1, days=5 - today.weekday())
        resv3_start = today + datetime.timedelta(weeks=2, days=5 - today.weekday())

    logging.info(f"resv1_start = {resv1_start}")
    logging.info(f"resv2_start = {resv2_start}")
    logging.info(f"resv3_start = {resv3_start}")

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "State"),
        lambda state: state == "IDLE",
        fatal=True,
    )

    logging.info(f"Creating resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv1 StartTime={resv1_start}T17:00 nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={resv1_flag}",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv2 can be created before resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv2 StartTime={resv2_start}T17:00 nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={resv2_flag}",
        user=atf.properties["slurm-user"],
    )
    assert (
        rc["exit_code"] == 0
    ), f"resv2 should be created before resv1 because WEEKDAYS and WEEKENDS won't overlap"

    logging.info(
        f"Double-checking that that there are 2 reservation with the expected node:"
    )
    assert len(atf.get_reservations()) == 2, f"resv1 and resv2 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"
    assert (
        atf.get_reservation_parameter("resv2", "Nodes") == nodes[0]
    ), f"resv2 should use node {nodes[0]}"

    logging.info(f"Deleting resv2:")
    rc = atf.run_command(
        f"scontrol delete ReservationName=resv2", user=atf.properties["slurm-user"]
    )
    assert rc["exit_code"] == 0, f"resv1 should be created"

    logging.info(f"Ensuring that resv3 can be created after resv1:")
    rc = atf.run_command(
        f"scontrol create reservation ReservationName=resv3 StartTime={resv3_start}T17:00 nodecnt=1 duration=00:15:00 user={atf.properties['test-user']} flags={resv2_flag}",
        user=atf.properties["slurm-user"],
    )
    assert (
        rc["exit_code"] == 0
    ), f"resv3 should be created after resv1 because WEEKDAYS and WEEKENDS won't overlap"

    logging.info(
        f"Double-checking that that there are 2 reservation with the expected node:"
    )
    assert len(atf.get_reservations()) == 2, f"resv1 and resv3 should be in the system"
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should use node {nodes[0]}"
    assert (
        atf.get_reservation_parameter("resv3", "Nodes") == nodes[0]
    ), f"resv3 should use node {nodes[0]}"
