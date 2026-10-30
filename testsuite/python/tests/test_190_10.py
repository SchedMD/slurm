############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A drained GPU that leaves the node stays identified in its accounting event.

The event table records every drained TRES in TresReference. An absent device
keeps its index. A drain whose device was configured away is dropped, so the
event stops naming it.

This uses a STATIC node. Configuring a drained GPU away requires reducing the node's
configured GRES and its fake_gpus.conf together (so the node is not marked
INVALID) while preserving the controller's node record, which holds the
saved UUID order. atf.set_node_parameter() restarts Slurm with
clean=False and does exactly that.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"  # require_nodes(1, ...) yields node1

# Fake GPU device UUIDs, in fake_gpus.conf device order.
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_accounting()
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    # Auto-drain a UUID-bearing device instead of downing the node when short.
    atf.require_config_parameter_includes("SlurmctldParameters", "drain_absent_gres")
    # Auto-drain works with config_overrides too (test_190_19), but it makes
    # the controller skip checking reported devices against slurm.conf, and
    # these tests cover the default. ATF enables it by default. Drop that
    # token and keep any other SlurmdParameters instead of replacing the list.
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_tty(3)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    atf.require_nodes(1, [("Gres", "gpu:a100:3"), ("CPUs", 2)])
    # get_node_events() queries the TresReference event format field, which only exists
    # in 26.11 sacctmgr; an older sacctmgr exits 1 on an unknown field.
    atf.require_version(
        (26, 11),
        component="bin/sacctmgr",
        reason="Ticket 25180: TresReference event format field added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_slurm_running()


def _clear_gpu_drains():
    """Resume every gpu drain, with every GPU present.

    Index=ALL leaves a device that is absent drained, so this only clears
    everything once the GPUs are back. A GPU that drain_absent_gres drained
    and that has returned is resumed like any other. A registration still in
    flight can drain again, so keep resuming until nothing remains.
    """

    def _cleared():
        result = atf.update_tres_drain(
            NODE, "gres/gpu", "Index=ALL", action="Resume", quiet=True
        )
        assert result["exit_code"] == 0, result
        return not (atf.get_node_tres_drained(NODE) or "")

    for _ in atf.timer(timeout=60, fatal=True):
        v = _cleared()
        if v:
            break


def _restore_three_uuid_gpus():
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3")
    # A test can leave a different saved order. The report above is then
    # refused, and State=RESUME drops that order. Register again so the
    # controller saves [A, B, C].
    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.start_slurmd(NODE)
    _clear_gpu_drains()


@pytest.fixture(autouse=True)
def restore_gpus(setup):
    """Leave every test with three UUID GPUs and nothing drained.

    These cases shrink Gres= and drop a drain, drain by UUID, or auto-drain
    an absent device.
    Without a function-scoped restore, leftover drains and GPU sets leak into
    the next test (and into later files if this one is not last).
    """
    yield
    # function_setup cancels jobs only after this teardown, and the restore
    # restarts the controller, so do not leave a job holding the GPUs.
    atf.cancel_all_jobs(fatal=True, quiet=True)
    _restore_three_uuid_gpus()


def test_configured_away_drain_leaves_the_event():
    """A drain whose device was configured away is dropped from TresReference."""

    # Devices A@0, B@1, C@2. Drain two of them by UUID.
    result = atf.update_tres_drain(
        NODE, "gres/gpu", f"Uuid={GPU_A},{GPU_B}", reason="rma"
    )
    assert result["exit_code"] == 0, result

    present = (
        f"gres/gpu:a100(IDX:0)(UUID:{GPU_A})," f"gres/gpu:a100(IDX:1)(UUID:{GPU_B})"
    )
    for _ in atf.timer(timeout=60, fatal=True):
        u = atf.get_open_node_event_tres_reference(NODE)
        if u == present:
            break
    assert re.search(
        r"(?:^|,)gres/gpu=2(?:,|$)", atf.get_open_node_event_tres(NODE) or ""
    ), f"both drained GPUs should be counted: {atf.get_node_events(NODE)!r}"

    # Pull GPU_B and configure two GPUs. GPU_C takes the slot GPU_B held, so
    # GPU_B's drain is dropped. GPU_A stays drained and the event stays open
    # naming only GPU_A.
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, GPU_A), (2, GPU_C)), 2)

    kept = f"gres/gpu:a100(IDX:0)(UUID:{GPU_A})"
    for _ in atf.timer(timeout=60, fatal=True):
        u = atf.get_open_node_event_tres_reference(NODE)
        if u == kept:
            break
    assert GPU_B not in (atf.get_node_tres_drained(NODE) or ""), atf.get_node_detail(
        NODE
    )

    # GPU_B holds no slot, so the event counts only GPU_A.
    assert re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", atf.get_open_node_event_tres(NODE) or ""
    ), f"dropped GPU_B should not be counted: {atf.get_node_events(NODE)!r}"

    # GPU_B returns after GPU_C and binds the new slot. The dropped drain does
    # not come back, so the two undrained GPUs can run a job and the event
    # still names only A.
    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, GPU_A), (2, GPU_C), (3, GPU_B)), 3)
    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:a100:2 --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=60
    ), f"GPU_B did not come back undrained: {atf.get_node_detail(NODE)}"
    for _ in atf.timer(timeout=60, fatal=True):
        u = atf.get_open_node_event_tres_reference(NODE)
        if u == kept:
            break
    assert re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", atf.get_open_node_event_tres(NODE) or ""
    ), f"GPU_B should stay uncounted: {atf.get_node_events(NODE)!r}"


def test_auto_drained_gpu_is_named_in_tres_reference():
    """drain_absent_gres records the empty slot's index, not a UUID.

    The slot is the one GPU_B is bound to, which earlier tests may have moved.
    The slot is empty, so the event names only that index.
    """
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (2, GPU_C))
    )
    atf.start_slurmd(NODE)

    for _ in atf.timer(timeout=60, fatal=True):
        u = atf.get_open_node_event_tres_reference(NODE)
        if bool(re.fullmatch(r"gres/gpu:a100\(IDX:\d+\)", u or "")):
            break


def test_uuidless_index_slide_matches_drainedtres():
    """Shrinking below a UUID-less drained index drops that drain from the event.

    _gres_bit_drain_resize() discards high bits when Gres= shrinks, and
    TresReference must agree with DrainedTRES afterwards.
    """
    atf.require_config_file(
        "fake_gpus.conf",
        atf.fake_gpus_conf((0, "(null)"), (1, "(null)"), (2, "(null)")),
    )
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3")
    # A registration with no UUID map drops an auto-drain of an empty index.
    # Resume anything still listed so the index drain below is the only one.
    _clear_gpu_drains()

    result = atf.update_tres_drain(NODE, "gres/gpu", "Index=0,2", reason="slide")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(timeout=60, fatal=True):
        u = atf.get_open_node_event_tres_reference(NODE)
        if u == "gres/gpu:a100(IDX:0,2)":
            break

    atf.set_gpu_set(NODE, atf.fake_gpus_conf((0, "(null)"), (1, "(null)")), 2)

    for _ in atf.timer(timeout=60, fatal=True):
        u = atf.get_open_node_event_tres_reference(NODE)
        if u == "gres/gpu:a100(IDX:0)":
            break

    drained = atf.get_node_tres_drained(NODE) or ""
    assert re.search(
        r"gpu\(IDX:0\)", drained
    ), f"DrainedTRES should keep index 0: {atf.get_node_detail(NODE)}"
    assert not re.search(
        r"gpu\(IDX:[^)]*2", drained
    ), f"DrainedTRES should have dropped index 2: {atf.get_node_detail(NODE)}"
