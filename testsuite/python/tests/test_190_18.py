############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES drains when the devices go away.

drain_absent_gres is on. A drain stays when slurmd reports no device of the
GRES, whether it is held by UUID or by index, so it is still in place when the
devices come back. A live Gres count of zero clears device drains, because the
devices are configured away.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"

GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"

NOT_FOUND = "not_found"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("SlurmctldParameters", "drain_absent_gres")
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B))
    )
    atf.require_nodes(1, [("Gres", "gpu:a100:2"), ("CPUs", 8)])
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: stable GPU slots and TRES drain added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_slurm_running()


def _detail():
    return atf.get_node_detail(NODE)


def _register(*devices, count, clean=True):
    """Make this device set the node's registration at this configured count."""
    atf.stop_slurmd(NODE, quiet=True)
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*devices))
    if clean:
        atf.stop_slurmctld(quiet=True)
    atf.set_node_parameter(NODE, "Gres", f"gpu:a100:{count}")
    if clean:
        atf.restart_slurmctld(clean=True, quiet=True)
    atf.start_slurmd(NODE)


def _reregister(*devices):
    """Register a new device set against the controller's saved state."""
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*devices))
    atf.start_slurmd(NODE)


def _wait_drained(fragment):
    for _ in atf.timer(timeout=60, fatal=True):
        drained = atf.get_node_tres_drained(NODE) or ""
        if fragment in drained:
            break


def _wait_state(present):
    """Poll until node state includes (True) or lacks (False) INVAL."""
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State") or ""
        if ("INVAL" in state) == present:
            break


def test_drain_stays_when_no_devices_remain():
    """A drain by UUID stays when slurmd reports no device of the GRES.

    That is what a GPU driver that failed to load looks like. With a saved
    UUID order every device is just absent: the node stays up and DEGRADED,
    the other GPU is drained as not_found, and the administrator's drain is
    still there once the GPUs are back, when the not_found drain clears.
    """
    _register((0, GPU_A), (1, GPU_B), count=2)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    atf.update_tres_drain(NODE, "gres/gpu", f"Uuid={GPU_A}", reason="hold", fatal=True)
    _wait_drained("hold")

    _reregister()
    _wait_drained(NOT_FOUND)
    state = atf.get_node_field(NODE, "State") or ""
    assert "INVAL" not in state, _detail()
    assert "DEGRADED" in state, _detail()
    assert "hold" in (atf.get_node_tres_drained(NODE) or ""), _detail()

    _reregister((0, GPU_A), (1, GPU_B))
    for _ in atf.timer(timeout=60, fatal=True):
        if NOT_FOUND not in (atf.get_node_tres_drained(NODE) or ""):
            break
    assert "hold" in (atf.get_node_tres_drained(NODE) or ""), _detail()


def test_index_drain_stays_when_no_devices_remain():
    """A drain held on an index stays when slurmd reports no device.

    Neither GPU reports a UUID, so the drain is held on its index. A
    registration with no device leaves it, so the node is INVAL and still
    DEGRADED, and the drain is there once the GPUs are back.
    """
    _register((0, "(null)"), (1, "(null)"), count=2)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="gone", fatal=True)
    _wait_drained("gone")
    assert "DEGRADED" in (atf.get_node_field(NODE, "State") or ""), _detail()

    _reregister()
    _wait_state(True)
    assert "gone" in (atf.get_node_tres_drained(NODE) or ""), _detail()
    assert "DEGRADED" in (atf.get_node_field(NODE, "State") or ""), _detail()

    _reregister((0, "(null)"), (1, "(null)"))
    _wait_state(False)
    assert "gone" in (atf.get_node_tres_drained(NODE) or ""), _detail()


def test_reconfigure_count_to_zero_clears_device_drains():
    """A live Gres count of zero is the one File= change the controller accepts."""
    _register((0, GPU_A), (1, GPU_B), count=2)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    assert (
        atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="ecc")["exit_code"]
        == 0
    ), "drain update should succeed"
    _wait_drained("ecc")
    result = atf.run_command(
        f"scontrol update NodeName={NODE} Gres=gpu:a100:0",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    assert result["exit_code"] == 0, result
    for _ in atf.timer(timeout=60, fatal=True):
        detail = _detail()
        if detail and "gpu:a100:0" in detail and ("ecc" not in detail):
            break
