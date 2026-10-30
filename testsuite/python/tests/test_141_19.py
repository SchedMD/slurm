############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""SuspendExcStates=DEGRADED keeps a degraded node from being powered down.

Two idle nodes pass SuspendTime. The one with a drained CPU is DEGRADED and
must stay powered up; the other must power down, which shows power save ran.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

DEGRADED_NODE = "node1"
CONTROL_NODE = "node2"

# Long enough to drain a CPU on DEGRADED_NODE before it is idle that long.
suspend_time = 30
suspend_timeout = 10


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Ticket 25180: TresName drain added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter_includes("SuspendExcStates", "DEGRADED")
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()

    atf.update_tres_drain(DEGRADED_NODE, "cpu", "Index=0", reason="exc", fatal=True)
    yield
    atf.update_tres_drain(
        DEGRADED_NODE, "cpu", "Index=ALL", action="Resume", quiet=True
    )


def test_degraded_node_is_not_powered_down():
    """Only the node that is not DEGRADED is powered down."""

    assert "DEGRADED" in atf.get_node_parameter(
        DEGRADED_NODE, "state"
    ), atf.get_node_detail(DEGRADED_NODE)

    assert atf.wait_for_node_state(
        CONTROL_NODE,
        "POWERED_DOWN",
        timeout=suspend_time + suspend_timeout + 30,
    ), f"power save did not run: {atf.get_node_detail(CONTROL_NODE)}"

    state = atf.get_node_parameter(DEGRADED_NODE, "state")
    assert "DEGRADED" in state, atf.get_node_detail(DEGRADED_NODE)
    assert not {"POWERED_DOWN", "POWERING_DOWN", "POWER_DOWN"} & set(
        state
    ), f"DEGRADED node was powered down: {atf.get_node_detail(DEGRADED_NODE)}"
