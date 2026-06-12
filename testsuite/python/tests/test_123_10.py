############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf
import logging


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((25, 11), "sbin/slurmctld")
    atf.require_nodes(4)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def get_and_down_nodes():
    logging.info("Getting the necessary 3 nodes:")
    nodes = atf.run_job_nodes("-N3 true", fatal=True)

    logging.info("Setting ALL nodes Down:")
    atf.run_command(
        "scontrol update nodename=ALL state=down" " reason=test_resv_overlap",
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
        "scontrol update nodename=ALL state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.mark.parametrize("resv_flag", ["", "HOURLY", "DAILY", "WEEKLY"])
def test_creation_no_overlap_maint(request, get_and_down_nodes, resv_flag):
    """Verify that new reservation creation does not
    allocate nodes from MAINT reservations"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming nodes {nodes[0]} and {nodes[1]}:")
    atf.run_command(
        f"scontrol update" f" nodename={nodes[0]},{nodes[1]}" f" state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "state"),
        lambda state: state == ["IDLE"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[1], "state"),
        lambda state: state == ["IDLE"],
        fatal=True,
    )

    maint_start = "NOW+1day" if resv_flag else "NOW"
    logging.info(
        f"Creating MAINT reservation resv1 on"
        f" node {nodes[0]} (StartTime={maint_start}):"
    )
    rc = atf.run_command(
        f"scontrol create reservation"
        f" ReservationName=resv1"
        f" StartTime={maint_start}"
        f" duration=7-00:00:00 user=root"
        f" nodes={nodes[0]} flags=MAINT",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, "MAINT reservation resv1 should be created"

    flags_str = f"flags={resv_flag}" if resv_flag else ""

    logging.info(f"Creating resv2 with nodecnt=1" f" ({resv_flag or 'normal'}):")
    rc = atf.run_command(
        f"scontrol create reservation"
        f" ReservationName=resv2 StartTime=NOW"
        f" nodecnt=1 duration=00:15:00"
        f" user={atf.properties['test-user']}"
        f" {flags_str}",
        user=atf.properties["slurm-user"],
    )
    assert (
        rc["exit_code"] == 0
    ), f"resv2 should be created since {nodes[1]} is available"

    logging.info("Verifying resv2 picked the non-MAINT node:")
    assert atf.get_reservation_parameter("resv2", "Nodes") == nodes[1], (
        f"resv2 should use non-MAINT node {nodes[1]}," f" not MAINT node {nodes[0]}"
    )

    logging.info("Verifying resv1 still has its MAINT node:")
    assert (
        atf.get_reservation_parameter("resv1", "Nodes") == nodes[0]
    ), f"resv1 should still use MAINT node {nodes[0]}"


@pytest.mark.parametrize("resv_flag", ["", "HOURLY", "DAILY", "WEEKLY"])
def test_creation_fails_when_only_maint_nodes(request, get_and_down_nodes, resv_flag):
    """Verify that new reservation creation fails if only
    MAINT nodes are available"""

    nodes = get_and_down_nodes

    logging.info(f"Resuming only node {nodes[0]}:")
    atf.run_command(
        f"scontrol update nodename={nodes[0]}" f" state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_node_parameter(nodes[0], "state"),
        lambda state: state == ["IDLE"],
        fatal=True,
    )

    maint_start = "NOW+1day" if resv_flag else "NOW"
    logging.info(
        f"Creating MAINT reservation resv1 on"
        f" node {nodes[0]} (StartTime={maint_start}):"
    )
    rc = atf.run_command(
        f"scontrol create reservation"
        f" ReservationName=resv1"
        f" StartTime={maint_start}"
        f" duration=7-00:00:00 user=root"
        f" nodes={nodes[0]} flags=MAINT",
        user=atf.properties["slurm-user"],
    )
    assert rc["exit_code"] == 0, "MAINT reservation resv1 should be created"

    flags_str = f"flags={resv_flag}" if resv_flag else ""

    logging.info(
        f"Attempting to create resv2 with nodecnt=1"
        f" ({resv_flag or 'normal'}) when only"
        f" MAINT nodes are available:"
    )
    rc = atf.run_command(
        f"scontrol create reservation"
        f" ReservationName=resv2 StartTime=NOW"
        f" nodecnt=1 duration=00:15:00"
        f" user={atf.properties['test-user']}"
        f" {flags_str}",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert rc["exit_code"] != 0, (
        f"resv2 should NOT be created because the only"
        f" available node {nodes[0]} is in a MAINT"
        f" reservation"
    )

    logging.info("Verifying only the MAINT reservation exists:")
    assert len(atf.get_reservations()) == 1, "Only resv1 (MAINT) should exist"
