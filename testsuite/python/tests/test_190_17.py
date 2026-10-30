############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Changing a node's GPU set and its configured Gres count.

drain_absent_gres is left unset. A registration that is short of the
configured count keeps the missing device's index, drains it, and marks the
node INVAL. The automatic drain is cleared when the device returns. Editing
slurm.conf so the count matches the devices that are left, in the saved
order, brings the node back.

A GPU that reports no UUID is not in that saved order. Its drain stays on
the index slurmd reports.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"

GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"
GPU_D = "GPU-dddddddd-3333-3333-3333-333333333333"
GPU_E = "GPU-eeeeeeee-4444-4444-4444-444444444444"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_excludes("SlurmctldParameters", "drain_absent_gres")
    # config_overrides would keep a short node up and hide the INVAL a Gres
    # edit is supposed to clear.
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    for tty in range(5):
        atf.require_tty(tty)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    atf.require_nodes(1, [("Gres", "gpu:a100:3"), ("CPUs", 2)])
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: stable GPU slots added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_slurm_running()


def _detail():
    return atf.get_node_detail(NODE)


def _state():
    return atf.get_node_field(NODE, "State") or ""


def _wait_configured(count):
    for _ in atf.timer(timeout=60, fatal=True):
        detail = _detail()
        if bool(detail and re.search(f"\\bGres=gpu:a100:{count}\\b", detail)):
            break


def _resume_node():
    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _clear_drains():
    atf.update_tres_drain(NODE, "gres/gpu", "Index=ALL", action="Resume", quiet=True)
    atf.update_tres_drain(NODE, "gres/gpu", "Uuid=ALL", action="Resume", quiet=True)


def _reset(*devices, count):
    """Register exactly these devices at this configured count, undrained.

    slurmd is stopped, then slurmctld is restarted with a clean state, so
    this device set is the node's first registration and slots bind in
    report order. The controller is stopped before the Gres edit:
    set_node_parameter() restarts with saved state while slurmctld is up.
    """
    atf.stop_slurmd(NODE, quiet=True)
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*devices))
    atf.stop_slurmctld(quiet=True)
    atf.set_node_parameter(NODE, "Gres", f"gpu:a100:{count}")
    atf.restart_slurmctld(clean=True, quiet=True)
    atf.start_slurmd(NODE)
    _clear_drains()
    _resume_node()
    for _ in atf.timer(timeout=60, fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if drained is None:
            break
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _wait_configured(count)


def _reported_index(uuid):
    """Return the index slurmd reports for uuid, then resume that drain."""
    result = atf.update_tres_drain(NODE, "gres/gpu", f"Uuid={uuid}", reason="pos")
    assert result["exit_code"] == 0, result

    def _index():
        match = re.fullmatch(
            r"gpu\(IDX:(\d+)\)\(pos\)", atf.get_node_tres_drained(NODE) or ""
        )
        return int(match.group(1)) if match else None

    for _ in atf.timer(timeout=60, fatal=True):
        index = _index()
        if index is not None:
            break
    index = _index()
    atf.update_tres_drain(NODE, "gres/gpu", f"Uuid={uuid}", action="Resume", quiet=True)
    for _ in atf.timer(timeout=60, fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if drained is None:
            break
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    return index


def _job_gres(count):
    """Run a job that asks for count GPUs and return its detailed scontrol text."""
    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:a100:{count} --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "RUNNING", timeout=60), atf.get_job(job_id)
    return atf.run_command_output(
        f"scontrol -d show job {job_id}", fatal=True, quiet=True
    )


def test_remove_gpu_then_lower_configured_count():
    """Register three GPUs, remove one, then set Gres one lower in slurm.conf.

    The short registration marks the node INVAL and drains it. The Gres edit
    clears the INVAL. The registration drain stays until State=RESUME. The two
    GPUs that remain are reported at indexes 0 and 1, and a job can take both.
    """
    _reset((0, GPU_A), (1, GPU_B), (2, GPU_C), count=3)
    assert _reported_index(GPU_A) == 0, "GPU_A should be reported at index 0"
    assert _reported_index(GPU_B) == 1, "GPU_B should be reported at index 1"
    assert _reported_index(GPU_C) == 2, "GPU_C should be reported at index 2"

    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B))
    )
    atf.start_slurmd(NODE)
    atf.wait_for_node_state(NODE, "INVALID_REG", fatal=True)
    assert "lower than configured" in _detail(), _detail()

    atf.set_node_parameter(NODE, "Gres", "gpu:a100:2")
    atf.wait_for_node_state(NODE, ["INVALID_REG", "DOWN"], reverse=True, fatal=True)
    assert "DRAIN" in _state(), _detail()
    _wait_configured(2)
    assert atf.get_node_tres_drained(NODE) is None, _detail()

    _resume_node()
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    assert _reported_index(GPU_A) == 0, "GPU_A should be reported at index 0"
    assert _reported_index(GPU_B) == 1, "GPU_B should be reported at index 1"
    detail = _job_gres(2)
    assert re.search(r"GRES=gpu:a100:2\(IDX:0-1\)", detail), detail


def test_absent_gpu_is_drained_at_its_index_until_it_returns():
    """Without drain_absent_gres, an absent GPU leaves the node INVAL.

    The configured count stays 3 and DrainedTRES names the missing GPU's
    index. drain_absent_gres keeps the node up instead; this module leaves it
    unset. The automatic drain ends when the device returns, with no resume.
    """
    _reset((0, GPU_A), (1, GPU_B), (2, GPU_C), count=3)

    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (2, GPU_C))
    )
    atf.start_slurmd(NODE, fatal=True)
    atf.wait_for_node_state(NODE, "INVALID_REG", fatal=True)
    drained = atf.get_node_tres_drained(NODE) or ""
    assert "gpu(IDX:1)" in drained, _detail()
    assert "not_found" in drained, _detail()

    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B), (2, GPU_C))
    assert atf.get_node_tres_drained(NODE) is None, _detail()


def test_removing_the_first_gpu_renumbers_the_ones_slurmd_reports():
    """Dropping GPU_A leaves GPU_B and GPU_C in the order slurmd reports.

    Nothing was drained, so the node has no drain record for GPU_A.
    """
    _reset((0, GPU_A), (1, GPU_B), (2, GPU_C), count=3)
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((1, GPU_B), (2, GPU_C)), 2)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _wait_configured(2)
    assert atf.get_node_tres_drained(NODE) is None, _detail()
    assert _reported_index(GPU_B) == 0, "GPU_B should be reported at index 0"
    assert _reported_index(GPU_C) == 1, "GPU_C should be reported at index 1"
    detail = _job_gres(2)
    assert re.search(r"GRES=gpu:a100:2\(IDX:0-1\)", detail), detail


def test_swap_puts_the_new_gpu_in_the_departed_slot():
    """Replacing GPU_B with GPU_D, at the same count, binds GPU_D to slot 1.

    GPU_A and GPU_C stay where they were. No drain was recorded, so GPU_B
    leaves no drain behind and the node is not DEGRADED.
    """
    _reset((0, GPU_A), (1, GPU_B), (2, GPU_C), count=3)
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, GPU_A), (1, GPU_D), (2, GPU_C)), 3)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _wait_configured(3)
    assert atf.get_node_tres_drained(NODE) is None, _detail()
    assert _reported_index(GPU_A) == 0, "GPU_A should be reported at index 0"
    assert _reported_index(GPU_D) == 1, "GPU_D should be reported at index 1"
    assert _reported_index(GPU_C) == 2, "GPU_C should be reported at index 2"
    detail = _job_gres(3)
    assert re.search(r"GRES=gpu:a100:3\(IDX:0-2\)", detail), detail


def test_adding_a_gpu_binds_it_to_the_new_slot():
    """Raising Gres from 2 to 3 and registering a new GPU binds that GPU at 2.

    GPU_A and GPU_B keep the slots they had at the smaller count.
    """
    _reset((0, GPU_A), (1, GPU_B), count=2)
    assert _reported_index(GPU_A) == 0, "GPU_A should be reported at index 0"
    assert _reported_index(GPU_B) == 1, "GPU_B should be reported at index 1"

    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_D)), 3)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _wait_configured(3)
    assert atf.get_node_tres_drained(NODE) is None, _detail()
    assert _reported_index(GPU_A) == 0, "GPU_A should be reported at index 0"
    assert _reported_index(GPU_B) == 1, "GPU_B should be reported at index 1"
    assert _reported_index(GPU_D) == 2, "GPU_D should be reported at index 2"
    detail = _job_gres(3)
    assert re.search(r"GRES=gpu:a100:3\(IDX:0-2\)", detail), detail


def test_excess_detected_gpus_stay_ignored_on_reregistration():
    """A repeat registration still ignores GPUs above the configured count."""
    _reset(
        (0, GPU_A),
        (1, GPU_B),
        (2, GPU_C),
        (3, GPU_D),
        (4, GPU_E),
        count=4,
    )
    _wait_configured(4)
    atf.restart_slurmd_with_fake_gpus(
        NODE,
        (0, GPU_A),
        (1, GPU_B),
        (2, GPU_C),
        (3, GPU_D),
        (4, GPU_E),
    )
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    assert "INVAL" not in _state(), _detail()


def test_uuidless_drop_keeps_the_drain_on_its_index():
    """Without a UUID, lowering the count keeps a drain on its index.

    The last device is the one removed, so the drained index is still in
    range. The node stays up and DEGRADED, and the undrained GPU is the one
    a one-GPU job gets.
    """
    _reset((0, "(null)"), (1, "(null)"), (2, "(null)"), count=3)
    result = atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="low")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(timeout=60, fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if drained == "gpu(IDX:0)(low)":
            break

    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, "(null)"), (1, "(null)")), 2)
    for _ in atf.timer(timeout=60, fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if drained == "gpu(IDX:0)(low)":
            break
    _wait_configured(2)
    state = _state()
    assert "INVAL" not in state, _detail()
    assert "DEGRADED" in state, _detail()
    detail = _job_gres(1)
    assert re.search(r"GRES=gpu:a100:1\(IDX:1\)", detail), detail
