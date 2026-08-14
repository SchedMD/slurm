############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify reservations set and clear NODE_STATE_RES and NODE_STATE_MAINT."""

import os
import time

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
res_name = f"res_{test_name}"
res_name2 = f"res2_{test_name}"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(3)
    atf.require_slurm_running()


@pytest.fixture()
def nodes():
    node_list = sorted(atf.get_nodes(quiet=True).keys())[:3]
    yield node_list

    slurm_user = atf.properties["slurm-user"]
    for name in [res_name, res_name2]:
        atf.run_command(f"scontrol delete reservationname={name}", user=slurm_user)
    atf.wait_for_node_state(
        node_list, ["RESERVED", "MAINTENANCE"], reverse=True, fatal=True
    )


def create_resv(name, node_list, flags="ignore_jobs", duration=60, start="now"):
    """Create a reservation on the nodes. Wait for the RESERVED state."""

    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"scontrol create reservationname={name} start={start} "
        f"duration={duration} nodes={','.join(node_list)} "
        f"user={slurm_user} flags={flags}",
        user=slurm_user,
        fatal=True,
    )
    if start != "now":
        return
    assert atf.wait_for_node_state(
        node_list, "RESERVED"
    ), f"{node_list} must reach the RESERVED state while {name} is active"


def node_state(node):
    """Return the current state string for one node."""

    return atf.get_node_parameter(node, "state")


def test_res_flag_cleared_on_delete(nodes):
    """Delete a reservation. The nodes lose the RESERVED state."""

    slurm_user = atf.properties["slurm-user"]
    create_resv(res_name, nodes[:2])

    assert (
        atf.get_node_parameter(nodes[0], "reservation") == res_name
    ), "The reservation field must show the name of the reservation"

    atf.run_command(
        f"scontrol delete reservationname={res_name}", user=slurm_user, fatal=True
    )

    assert atf.wait_for_node_state(
        nodes[:2], "RESERVED", reverse=True
    ), f"{nodes[:2]} must lose the RESERVED state after the delete"
    assert not atf.get_node_parameter(
        nodes[0], "reservation"
    ), "The reservation field must be empty after the delete"


@pytest.mark.slow
def test_res_flag_cleared_on_expiry(nodes):
    """A reservation stops at its end time. The nodes lose the RESERVED
    state. No delete command is needed."""

    create_resv(res_name, nodes[:2], duration=1)

    # The reservation is one minute long. That is more than the default poll
    # timeout, so use a longer one.
    assert atf.wait_for_node_state(
        nodes[:2], "RESERVED", reverse=True, timeout=120
    ), f"{nodes[:2]} must lose the RESERVED state after the reservation ends"


def test_res_flag_cleared_on_shrink(nodes):
    """Remove one node from a reservation. That node loses the RESERVED
    state. The other node keeps it."""

    slurm_user = atf.properties["slurm-user"]
    create_resv(res_name, nodes[:2])

    atf.run_command(
        f"scontrol update reservationname={res_name} nodes={nodes[0]}",
        user=slurm_user,
        fatal=True,
    )

    # nodes[1] losing RESERVED is the barrier: it proves the recompute the
    # update triggers has run before nodes[0] is read below. One snapshot
    # after the wait keeps the two assertions on nodes[0] consistent with
    # each other.
    assert atf.wait_for_node_state(
        nodes[1], "RESERVED", reverse=True
    ), f"{nodes[1]} must lose the RESERVED state after you remove it"
    nodes_now = atf.get_nodes(quiet=True)
    assert not nodes_now[nodes[1]][
        "reservation"
    ], f"The reservation field on {nodes[1]} must be empty after you remove it"
    assert (
        "RESERVED" in nodes_now[nodes[0]]["state"]
    ), f"{nodes[0]} must keep the RESERVED state because it stays in the reservation"
    assert (
        nodes_now[nodes[0]]["reservation"] == res_name
    ), f"The reservation field on {nodes[0]} must still show {res_name}"


def test_maint_flag_cleared_on_delete(nodes):
    """A MAINT reservation sets the MAINTENANCE state. A delete of the
    reservation clears this state."""

    slurm_user = atf.properties["slurm-user"]
    create_resv(res_name, nodes[:2], flags="maint,ignore_jobs")

    assert atf.wait_for_node_state(
        nodes[0], "MAINTENANCE"
    ), "The MAINT reservation must set the MAINTENANCE state"

    atf.run_command(
        f"scontrol delete reservationname={res_name}", user=slurm_user, fatal=True
    )

    assert atf.wait_for_node_state(
        nodes[0], "MAINTENANCE", reverse=True
    ), "The MAINTENANCE state must clear after the delete"


def test_maint_kept_while_second_maint_resv_covers_node(nodes):
    """A node is in two MAINT reservations. Delete one reservation. The node
    keeps the MAINTENANCE state."""

    slurm_user = atf.properties["slurm-user"]
    create_resv(res_name, nodes[:2], flags="maint,ignore_jobs")
    create_resv(res_name2, [nodes[0], nodes[2]], flags="maint,ignore_jobs,overlap")

    atf.run_command(
        f"scontrol delete reservationname={res_name2}", user=slurm_user, fatal=True
    )

    # nodes[2] is only in the deleted reservation. It must lose both states,
    # which proves the recompute ran before the states below were read.
    assert atf.wait_for_node_state(
        nodes[2], ["RESERVED", "MAINTENANCE"], reverse=True
    ), f"{nodes[2]} must lose both states after the delete"

    for node in nodes[:2]:
        assert "MAINTENANCE" in node_state(
            node
        ), f"{node} must keep the MAINTENANCE state because {res_name} covers it"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Earlier versions clear both flags from the overlapped node until "
    "the next recompute",
)
def test_maint_clears_but_res_stays_on_mixed_overlap(nodes):
    """A node is in a MAINT reservation and a plain reservation. Delete the
    MAINT reservation. The node loses MAINTENANCE and keeps RESERVED."""

    slurm_user = atf.properties["slurm-user"]
    create_resv(res_name, nodes[:2])
    create_resv(res_name2, [nodes[0], nodes[2]], flags="maint,ignore_jobs,overlap")

    assert atf.wait_for_node_state(
        nodes[0], "MAINTENANCE"
    ), f"{nodes[0]} must reach the MAINTENANCE state from {res_name2}"

    atf.run_command(
        f"scontrol delete reservationname={res_name2}", user=slurm_user, fatal=True
    )

    assert atf.wait_for_node_state(
        nodes[2], "RESERVED", reverse=True
    ), f"{nodes[2]} must lose the RESERVED state after the delete"

    nodes_now = atf.get_nodes(quiet=True)
    state = nodes_now[nodes[0]]["state"]
    assert (
        "MAINTENANCE" not in state
    ), f"{nodes[0]} must lose the MAINTENANCE state with {res_name2} gone"
    assert (
        "RESERVED" in state
    ), f"{nodes[0]} must keep the RESERVED state from {res_name}"
    assert (
        nodes_now[nodes[0]]["reservation"] == res_name
    ), f"The reservation field on {nodes[0]} must fall back to {res_name}"


@pytest.mark.slow
def test_future_reservation_leaves_nodes_unreserved(nodes):
    """A reservation that starts later does not set the RESERVED state. The
    nodes get the state when the start time arrives."""

    # duration is minutes, start is seconds; use a duration that does not
    # collide with the start offset below so the two are not misread as the
    # same unit.
    create_resv(res_name, nodes[:2], start="now+60", duration=5)

    # Poll the reservation's own State field instead of sleeping a fixed
    # amount of wall-clock time. That ties the negative check to slurmctld's
    # own answer for whether the reservation is active yet, so a slow
    # 'scontrol create' or a loaded controller cannot turn a *correct* late
    # RESERVED flag into a false failure here.
    deadline = time.time() + 90
    while atf.get_reservation_parameter(res_name, "State") == "INACTIVE":
        assert time.time() < deadline, f"{res_name} never left the INACTIVE state"
        for node in nodes[:2]:
            assert "RESERVED" not in node_state(
                node
            ), f"{node} must not get the RESERVED state before {res_name} starts"
            assert not atf.get_node_parameter(
                node, "reservation"
            ), f"The reservation field on {node} must be empty before {res_name} starts"
        time.sleep(2)

    # The reservation starts 60s after creation, past the default poll
    # timeout, so use a longer one.
    assert atf.wait_for_node_state(
        nodes[:2], "RESERVED", timeout=120
    ), f"{nodes[:2]} must get the RESERVED state when {res_name} starts"
