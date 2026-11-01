############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""SlurmctldParameters=drain_absent_gres auto-drains a GPU that goes absent.

When a UUID-bearing device GRES (a GPU under NVML/full autodetect) reports fewer
devices than are configured, the default is to mark the whole node INVAL. With
``SlurmctldParameters=drain_absent_gres`` the controller drains the empty index
and keeps the node up. The absent GPU keeps its index. ``DrainedTRES`` names
that index. The automatic ``not_found`` drain is cleared when a device fills
the index. The drain counts toward ``MaxDrainedTres`` for its type.

This uses a STATIC node and restarts *only slurmd* (``atf.start_slurmd``) between
device-set changes. That is deliberate: the controller compares the
registration with the UUID order saved from an earlier registration. That
order is saved with the node state, so a controller restart does not lose it.
"""

import pathlib
import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"  # require_nodes(1, ...) yields node1

# Fake GPU device UUIDs, in fake_gpus.conf device order. _reset() binds them
# to slots 0, 1 and 2.
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"
GPU_INDEX = {GPU_A: 0, GPU_B: 1, GPU_C: 2}
# A replacement GPU, never seen before.
GPU_D = "GPU-dddddddd-3333-3333-3333-333333333333"
GPU_E = "GPU-eeeeeeee-4444-4444-4444-444444444444"

# Auto-drain reason recorded for an absent device (see _slot_drain_absent).
NOT_FOUND = "not_found"
UUID_ORDER_DRAIN = "GPU UUID order does not match the saved order"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Before any node line is written. 26.05 scontrol rejects MaxDrainedTres,
    # and require_nodes() asks scontrol to expand the node name.
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: SlurmctldParameters=drain_absent_gres added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots and MaxDrainedTres added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")
    atf.require_config_parameter_includes("GresTypes", "bandwidth")
    atf.require_config_parameter_includes(
        "SelectTypeParameters", "MULTIPLE_SHARING_GRES_PJ"
    )
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
    # Shards are configured as a count, not per device file: the controller
    # then spreads them over the sharing GPUs itself, which is the placement a
    # absent GPU has to be kept out of. A File-per-shard gres.conf instead
    # makes the reported shard count drop with the GPU, and shared GRES never
    # takes part in auto-drain, so the node would go INVAL before any of that.
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    # Typed limit. An absent GPU keeps the type of its slot, so one absent
    # a100 stays within it and two exceed it.
    atf.require_nodes(
        1,
        [
            ("Gres", "gpu:a100:3,shard:300"),
            ("CPUs", 2),
            ("MaxDrainedTres", "gres/gpu:a100:1"),
        ],
    )
    atf.require_slurm_running()


def _register_slurmd(*devices):
    """Rewrite fake_gpus.conf and restart slurmd, without requiring the node up.

    Without drain_absent_gres a registration short of the configured count
    leaves the node INVAL, which ``atf.restart_slurmd_with_fake_gpus`` treats
    as a failure to settle.
    """
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*devices))
    atf.start_slurmd(NODE)


def _holds_all_gpus(job_id):
    """True when the job's GRES allocation is still IDX:0-2."""
    return (
        re.search(r"gpu(?::a100)?:3\(IDX:0-2\)", str(atf.get_job(job_id))) is not None
    )


def _hold_highest_gpu():
    """Return a running job that holds every GPU, including the highest index.

    The node has two CPUs, so three one-GPU jobs cannot all run. One job
    asking for all three GPUs holds index 2, which a shrink from three
    devices to two would cut off.
    """
    _reset()
    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:a100:3 --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "RUNNING", timeout=60), str(
        atf.get_job(job_id)
    )
    assert _holds_all_gpus(job_id), str(atf.get_job(job_id))
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE) or ""
        if re.search(r"GresUsed=gpu:(?:[^:]*:)?3\(IDX:0-2\)", detail):
            break
    return job_id


@pytest.fixture
def restore_drain_absent_gres():
    """Put drain_absent_gres back where the module has it after the test.

    The module needs it in SlurmctldParameters and not in SlurmdParameters.
    A test that moves it would otherwise leave the later tests without it.
    """
    yield
    atf.require_config_parameter_excludes("SlurmdParameters", "drain_absent_gres")
    atf.require_config_parameter_includes("SlurmctldParameters", "drain_absent_gres")


def _reset():
    """Restore all three GPUs and clear every drain, leaving the node clean.

    Establishes the full [A, B, C] UUID map on the controller (the baseline a
    subsequent removal is diffed against) and clears any GRES drain or node DRAIN
    a prior test left behind.
    """
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B), (2, GPU_C))
    # All previously-absent devices are present again; clear their drains, then
    # undo any node-level DRAIN from an over-limit escalation.
    atf.update_tres_drain(NODE, "gres/gpu", "Index=ALL", action="Resume", quiet=True)
    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    # A prior test can leave a different saved order. The first registration
    # is then refused, and State=RESUME drops that order. Register again so
    # the controller saves [A, B, C].
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        f = atf.get_node_tres_drained(NODE)
        if f is None:
            break


def _uuid_drained(uuid, reason=NOT_FOUND):
    """DrainedTRES lists an absent device by the UUID's saved index.

    Adjacent drained indexes share one range, such as gpu(IDX:0-1).
    """

    def _pred(detail):
        return any(
            GPU_INDEX[uuid] in atf.range_to_list(idx) and reason in why
            for idx, why in re.findall(
                r"gpu\(IDX:([-0-9,]+)\)\(([^)]*)\)", detail or ""
            )
        )

    return _pred


def _wait_drained(predicate):
    for _ in atf.timer(timeout=60, fatal=True):
        _value = atf.get_node_detail(NODE)
        if predicate(_value):
            break


def test_absent_gpu_auto_drains_and_node_stays_up():
    """A GPU reported absent is drained in its slot; the node stays up and
    DEGRADED (not INVAL/DOWN) with the absent GPU shown in DrainedTRES."""
    _reset()

    # Drop GPU_B (slot 1); GPU_A and GPU_C remain. Gres stays gpu:a100:3.
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))

    for _ in atf.timer(timeout=60, fatal=True):
        _value = atf.get_node_detail(NODE)
        if (_uuid_drained(GPU_B))(_value):
            break

    state = atf.get_node_field(NODE, "State")
    assert "INVAL" not in state, atf.get_node_detail(NODE)
    assert "DOWN" not in state, atf.get_node_detail(NODE)
    # One absent a100 is within MaxDrainedTres=gres/gpu:a100:1.
    assert "DRAIN" not in state, atf.get_node_detail(NODE)
    assert "DEGRADED" in state, atf.get_node_detail(NODE)


def test_all_gpus_absent_keep_shards_drained():
    """Losing every sharing GPU keeps the shard layout, every shard drained.

    With drain_absent_gres, a registration with no GPU is every GPU absent:
    the node stays up and each slot is drained, so the shards on them are
    shown drained rather than dropped, ready for when the GPUs come back.
    """
    _reset()
    assert re.search(
        r"shard:a100:0\(0/100,0/100,0/100\)", atf.get_node_detail(NODE)
    ), atf.get_node_detail(NODE)

    _register_slurmd()
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE) or ""
        if "d/100,d/100,d/100" in detail:
            break
    state = atf.get_node_field(NODE, "State") or ""
    assert "INVAL" not in state, atf.get_node_detail(NODE)


def test_gpus_all_absent_drain_only_until_back():
    """A registration with no GPU drains every slot until the GPUs are back.

    That is what a GPU driver that failed to load looks like. Each empty slot
    is drained as not_found and the node stays up. The drains clear when the
    GPUs are reported again, so nothing outlives the absence.
    """
    _reset()
    _register_slurmd()
    _wait_drained(
        lambda detail: bool(
            detail and re.search(rf"gpu\(IDX:0-2\)\([^)]*{NOT_FOUND}", detail)
        )
    )
    state = atf.get_node_field(NODE, "State") or ""
    assert "INVAL" not in state, atf.get_node_detail(NODE)

    _register_slurmd((0, GPU_A), (1, GPU_B), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        if atf.get_node_tres_drained(NODE) is None:
            break


def test_auto_drain_is_idempotent_across_reregistration():
    """Re-registering with the same absent GPU does not re-INVAL the node or
    double-count the drain."""
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))

    # Register again with the identical reduced set, waiting for the NEW
    # registration to be processed before asserting (else we would read the
    # stale first-registration state and the idempotence check is vacuous).
    atf.start_slurmd(NODE)
    for _ in atf.timer(timeout=60, fatal=True):
        s = atf.get_node_field(NODE, "State")
        if s is not None and "DEGRADED" in s and ("INVAL" not in s):
            break

    detail = atf.get_node_detail(NODE)
    assert "DOWN" not in atf.get_node_field(NODE, "State"), detail
    assert "DRAIN" not in atf.get_node_field(NODE, "State"), detail
    # Exactly one device is drained -- no duplicate/second projection.
    assert (
        atf.get_node_tres_drained(NODE) == f"gpu(IDX:1)(gres/gpu:{NOT_FOUND})"
    ), detail


@pytest.mark.parametrize(
    ("devices", "fitting"),
    [
        (
            ((0, GPU_B), (1, GPU_A), (2, GPU_C)),
            ((0, GPU_A), (1, GPU_B), (2, GPU_C)),
        ),
        (
            ((0, GPU_A), (2, GPU_C), (3, GPU_D)),
            ((0, GPU_A), (2, GPU_C)),
        ),
    ],
    ids=["remaining_uuid_moved", "new_uuid_appended_past_hole"],
)
def test_refused_uuid_order_drains_until_a_report_fits(devices, fitting):
    """A moved UUID or an append past a hole drains without changing slots."""
    _reset()
    if fitting != ((0, GPU_A), (1, GPU_B), (2, GPU_C)):
        atf.restart_slurmd_with_fake_gpus(NODE, *fitting)
        _wait_drained(_uuid_drained(GPU_B))

    _register_slurmd(*devices)
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE) or ""
        state = atf.get_node_field(NODE, "State") or ""
        if "DRAIN" in state and UUID_ORDER_DRAIN in detail:
            break
    assert "INVAL" not in (atf.get_node_field(NODE, "State") or ""), detail

    _register_slurmd(*fitting)
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE) or ""
        state = atf.get_node_field(NODE, "State") or ""
        if "DRAIN" not in state and "INVAL" not in state:
            break
    assert UUID_ORDER_DRAIN not in detail, detail


def test_resuming_uuid_order_drain_accepts_replacement_order():
    """State=RESUME drops the refused order for replacement cards."""
    _reset()
    replacement_order = ((0, GPU_B), (1, GPU_A), (2, GPU_C))

    _register_slurmd(*replacement_order)
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE) or ""
        state = atf.get_node_field(NODE, "State") or ""
        if "DRAIN" in state and UUID_ORDER_DRAIN in detail:
            break

    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _register_slurmd(*replacement_order)
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE) or ""
        state = atf.get_node_field(NODE, "State") or ""
        if "DRAIN" not in state and "INVAL" not in state:
            break
    assert UUID_ORDER_DRAIN not in detail, detail


def test_resuming_uuid_order_drain_resumes_uuid_drains():
    """State=RESUME of the UUID-order drain resumes the drains by UUID too.

    GPU_A is drained by UUID before the refused order. After State=RESUME a
    report without GPU_B drains only GPU_B's slot, and GPU_A stays resumed.
    """
    _reset()
    atf.update_tres_drain(NODE, "gres/gpu", f"Uuid={GPU_A}", reason="admin", fatal=True)
    replacement_order = ((0, GPU_B), (1, GPU_A), (2, GPU_C))

    _register_slurmd(*replacement_order)
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE) or ""
        state = atf.get_node_field(NODE, "State") or ""
        if "DRAIN" in state and UUID_ORDER_DRAIN in detail:
            break

    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.restart_slurmd_with_fake_gpus(NODE, *replacement_order)
    assert atf.get_node_tres_drained(NODE) is None, atf.get_node_detail(NODE)

    # The saved order is now B, A, C, so GPU_B is index 0.
    atf.restart_slurmd_with_fake_gpus(NODE, (1, GPU_A), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        drained = atf.get_node_tres_drained(NODE) or ""
        if NOT_FOUND in drained:
            break
    assert re.fullmatch(r"gpu\(IDX:0\)\([^)]*not_found\)", drained), drained


def test_replacement_gpu_takes_the_slot_and_old_drain_is_dropped():
    """A replacement GPU takes the absent GPU's slot.

    Its new UUID is bound to the empty slot. The drain of the old UUID lost
    that slot and is dropped, so the node has three undrained GPUs.
    """
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))

    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_D), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        f = atf.get_node_tres_drained(NODE)
        if f is None:
            break
    state = atf.get_node_field(NODE, "State")
    assert "INVAL" not in state and "DRAIN" not in state, atf.get_node_detail(NODE)
    assert "DEGRADED" not in state, atf.get_node_detail(NODE)

    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:a100:3 --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), atf.get_node_detail(NODE)
    assert _holds_all_gpus(job_id), str(atf.get_job(job_id))


def test_gpu_lost_while_slurmctld_was_down_is_auto_drained():
    """A GPU that disappears while the controller is down is still named.

    An absent device is identified by the slot its UUID was bound to, so the
    slots are saved with the node state. A controller that came back without
    them would see only a device set short of the configured count, with
    nothing to say which UUID is gone, and would INVAL the node instead of
    draining the device that went away.
    """
    _reset()

    # Stop slurmd first: the controller must recover the slots from state, not
    # from a registration that happens to arrive after it restarts.
    atf.stop_slurmd(NODE, quiet=True)
    atf.restart_slurmctld(clean=False, quiet=True)
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))

    for _ in atf.timer(timeout=60, fatal=True):
        _value = atf.get_node_detail(NODE)
        if (_uuid_drained(GPU_B))(_value):
            break
    assert "INVAL" not in atf.get_node_field(NODE, "State"), atf.get_node_detail(NODE)


def test_replacing_two_absent_gpus_drops_both_drains():
    """Replacing two absent GPUs drops both drains.

    GPU_A and GPU_B are absent and drained in their slots, which is over the
    typed MaxDrainedTres limit. GPU_D and GPU_E then take those slots, and
    both drains are dropped.
    """
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (2, GPU_C))
    _wait_drained(lambda d: _uuid_drained(GPU_A)(d) and _uuid_drained(GPU_B)(d))
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_D), (1, GPU_E), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        f = atf.get_node_tres_drained(NODE)
        if f is None:
            break
    state = atf.get_node_field(NODE, "State") or ""
    assert "DEGRADED" not in state, atf.get_node_detail(NODE)


def test_absent_gpus_count_toward_their_type_limit():
    """An absent GPU counts toward MaxDrainedTres for the type of its slot.

    MaxDrainedTres=gres/gpu:a100:1 allows one absent a100, so two drain the
    node without marking it DOWN or INVAL.
    """
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (2, GPU_C))

    for _ in atf.timer(timeout=60, fatal=True):
        s = atf.get_node_field(NODE, "State")
        if s is not None and "DRAIN" in s and ("DOWN" not in s) and ("INVAL" not in s):
            break
    drained = atf.get_node_tres_drained(NODE)
    assert _uuid_drained(GPU_A)(drained) and _uuid_drained(GPU_B)(drained), drained


def test_absent_gpu_cannot_be_resumed():
    """An empty slot stays drained until a device fills it.

    Index= of that slot is refused. Index=ALL and State=RESUME clear a drain
    on a GPU that is still there, leave the absent one, and say so.
    """
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))

    refused = atf.update_tres_drain(NODE, "gres/gpu", "Index=1", action="Resume")
    assert refused["exit_code"] != 0, refused
    assert "Absent GRES device cannot be resumed" in refused["stderr"], refused
    assert "Absent GRES stays drained" not in refused["stderr"], refused
    assert _uuid_drained(GPU_B)(atf.get_node_detail(NODE)), atf.get_node_detail(NODE)

    held = atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="hold")
    assert held["exit_code"] == 0, held
    cleared = atf.update_tres_drain(NODE, "gres/gpu", "Index=ALL", action="Resume")
    assert cleared["exit_code"] == 0, cleared
    assert "Absent GRES stays drained" in cleared["stderr"], cleared
    assert "index 1" in cleared["stderr"], cleared
    detail = atf.get_node_detail(NODE)
    assert _uuid_drained(GPU_B)(detail), detail
    assert "hold" not in detail, detail

    resumed = atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
    )
    assert resumed["exit_code"] == 0, resumed
    assert "Absent GRES stays drained" in resumed["stderr"], resumed
    assert "index 1" in resumed["stderr"], resumed
    assert _uuid_drained(GPU_B)(atf.get_node_detail(NODE)), atf.get_node_detail(NODE)


def test_absent_slot_uuid_does_not_select_the_empty_slot():
    """Uuid does not name an empty slot. The drain stays."""
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))

    missed = atf.update_tres_drain(NODE, "gres/gpu", f"Uuid={GPU_B}", action="Resume")
    assert missed["exit_code"] != 0, missed
    assert "Requested GRES UUID not found on node" in missed["stderr"], missed
    assert _uuid_drained(GPU_B)(atf.get_node_detail(NODE)), atf.get_node_detail(NODE)


def test_set_drain_of_a_present_index_leaves_the_absent_index():
    """SetDrain of a present index does not clear an absent index.

    scontrol names the index it leaves drained.
    """
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))
    held = atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="hold")
    assert held["exit_code"] == 0, held

    replaced = atf.update_tres_drain(
        NODE, "gres/gpu", "Index=0", action="SetDrain", reason="hold"
    )
    assert replaced["exit_code"] == 0, replaced
    assert "Absent GRES stays drained" in replaced["stderr"], replaced
    assert "index 1" in replaced["stderr"], replaced
    detail = atf.get_node_detail(NODE)
    assert _uuid_drained(GPU_B)(detail), detail
    assert re.search(r"gpu\(IDX:0\)\(hold\)", detail), detail


def test_show_drainedtres_names_an_absent_index_and_no_uuid():
    """show drainedtres keeps an empty slot's index and prints no Uuid."""
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))
    output = atf.run_command_output(
        f"scontrol show drainedtres NodeName={NODE}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    lines = [ln for ln in output.splitlines() if ln.startswith("NodeName=")]
    assert len(lines) == 1, output
    assert "Index=1" in lines[0], output
    assert "Uuid=" not in lines[0], output


def test_admin_drain_reason_survives_drop_and_return():
    """An administratively drained GPU that then goes absent keeps its admin
    reason (auto-drain does not clobber it) and its slot."""
    _reset()

    # Admin-drain GPU_B (present, slot 1) with a distinct reason.
    result = atf.update_tres_drain(
        NODE, "gres/gpu", f"Uuid={GPU_B}", reason="admin_held"
    )
    assert result["exit_code"] == 0, result
    assert re.search(
        r"gpu\(IDX:1\)\(admin_held\)", atf.get_node_detail(NODE)
    ), atf.get_node_detail(NODE)

    # Drop GPU_B: it is absent but must keep the admin reason, not be
    # relabeled as an auto "not_found" drain.
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        d = atf.get_node_tres_drained(NODE)
        if d == "gpu(IDX:1)(admin_held)":
            break
    assert "INVAL" not in atf.get_node_field(NODE, "State"), atf.get_node_detail(NODE)

    # GPU_B returns to its slot, still with the admin reason.
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        d = atf.get_node_detail(NODE)
        if bool(re.search("gpu\\(IDX:1\\)\\(admin_held\\)", d)):
            break

    # Clean up so a later run starts fresh.
    atf.update_tres_drain(NODE, "gres/gpu", "Index=ALL", action="Resume", quiet=True)


def _visible_gpus(job_id):
    """GPU indexes a 2-GPU step of ``job_id`` is given, from CUDA_VISIBLE_DEVICES.

    The step asks for the GPUs itself: a whole-node job is granted every GPU
    without requesting any, and a step only inherits the GRES its job requested.
    """
    output = atf.run_job_output(
        f"--jobid={job_id} --overlap --gres=gpu:2 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    return [dev for dev in output.strip().split(",") if dev]


@pytest.mark.parametrize(
    "job_args",
    ["--gres=gpu:2", "--exclusive"],
    ids=["gres_gpu_2", "whole_node"],
)
def test_absent_gpu_job_is_bound_to_every_granted_gpu(job_args):
    """With an absent GPU, a job is bound to every GPU it was granted.

    The present GPUs keep controller slots 0 and 2. slurmd numbers the two
    GPUs it finds 0 and 1, and the copy sent to slurmd uses those indexes.
    scontrol keeps 0 and 2. Both the scheduler's pick (--gres) and the
    whole-node grant (--exclusive) are checked.
    """
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))

    job_id = atf.submit_job_sbatch(
        f"-w {NODE} {job_args} --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"job did not run with an absent GPU: {atf.get_node_detail(NODE)}"
    job = str(atf.get_job(job_id))
    assert re.search(r"gpu(?::a100)?:2\(IDX:0,2\)", job), job
    assert sorted(_visible_gpus(job_id)) == ["0", "1"], job


@pytest.fixture
def prolog_epilog(tmp_path):
    """Record each job's CUDA_VISIBLE_DEVICES from Prolog and Epilog.

    Yields the directory the scripts write prolog.<jobid> and epilog.<jobid>
    to, and unsets both parameters afterwards.
    """
    prolog_dir = pathlib.Path(tmp_path)
    prolog_dir.chmod(0o777)
    for name in ("prolog", "epilog"):
        out = f"{prolog_dir}/{name}.$SLURM_JOB_ID"
        atf.make_bash_script(
            str(prolog_dir / f"{name}.sh"),
            "\n".join(
                [
                    f'echo "$CUDA_VISIBLE_DEVICES" > {out}',
                    f"chmod a+r {out}",
                ]
            ),
        )
    atf.set_config_parameter("Prolog", str(prolog_dir / "prolog.sh"))
    atf.set_config_parameter("Epilog", str(prolog_dir / "epilog.sh"))

    yield prolog_dir

    atf.set_config_parameter("Prolog", None)
    atf.set_config_parameter("Epilog", None)


def test_prolog_and_epilog_see_slurmd_indexes_across_a_hole(prolog_epilog):
    """Prolog and epilog are given slurmd's indexes across a hole.

    The job is granted controller indexes 0 and 2. slurmd has those GPUs
    at 0 and 1, and both scripts record that CUDA_VISIBLE_DEVICES.
    """
    _reset()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))

    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:2 --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "RUNNING", timeout=60), str(
        atf.get_job(job_id)
    )
    job = str(atf.get_job(job_id))
    assert re.search(r"gpu(?::a100)?:2\(IDX:0,2\)", job), job

    atf.assert_file_contents(str(prolog_epilog / f"prolog.{job_id}"), "0,1")

    # The epilog runs when the job is cancelled too.
    atf.cancel_jobs([job_id], fatal=True)
    atf.assert_file_contents(str(prolog_epilog / f"epilog.{job_id}"), "0,1")


def test_reconfigure_toggles_drain_absent_gres(restore_drain_absent_gres):
    """The controller setting governs, and a reconfigure is enough.

    With the token only in SlurmdParameters, an absent GPU makes the node
    INVAL ("count reported lower than configured"). Its slot stays bound to
    its UUID, so once the token is in SlurmctldParameters the next
    registration with the GPU still absent drains it and keeps the node up.
    """
    _reset()
    atf.require_config_parameter_excludes("SlurmctldParameters", "drain_absent_gres")
    atf.require_config_parameter_includes("SlurmdParameters", "drain_absent_gres")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (2, GPU_C))
    )
    atf.start_slurmd(NODE)
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE)
        if (
            detail is not None
            and "INVAL" in detail
            and ("lower than configured" in detail)
        ):
            break

    atf.require_config_parameter_excludes("SlurmdParameters", "drain_absent_gres")
    atf.require_config_parameter_includes("SlurmctldParameters", "drain_absent_gres")
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (2, GPU_C))
    _wait_drained(_uuid_drained(GPU_B))
    state = atf.get_node_field(NODE, "State")
    assert "INVAL" not in state, atf.get_node_detail(NODE)
    assert "DEGRADED" in state, atf.get_node_detail(NODE)


def _held_gpu_stays_allocated_across_reregistration():
    """A job holding the highest GPU keeps it when that GPU goes absent.

    Without drain_absent_gres the node goes INVAL and the job stays RUNNING.
    The registration is refused, so the job and GresUsed still name index 2.
    Restoring that GPU and resuming must not hand it to a second job of the
    same type while the first job runs.
    """
    job_id = _hold_highest_gpu()
    _register_slurmd((0, GPU_A), (1, GPU_B))
    for _ in atf.timer(timeout=60, fatal=True):
        detail = atf.get_node_detail(NODE)
        if detail is not None and "INVAL" in detail:
            break
    assert atf.wait_for_job_state(job_id, "RUNNING"), str(atf.get_job(job_id))
    detail = atf.get_node_detail(NODE)
    assert re.search(r"GresUsed=gpu:(?:[^:]*:)?3\(IDX:0-2\)", detail), detail
    job = str(atf.get_job(job_id))
    assert re.search(r"gpu(?::a100)?:3\(IDX:0-2\)", job), job

    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B), (2, GPU_C))
    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State")
        if state is not None and "INVAL" not in state and ("DOWN" not in state):
            break
    _second_job_pends(job_id)


def _second_job_pends(job_id, idx="0-2"):
    """A one-GPU job pends while job_id holds every GPU index.

    ``idx`` is job_id's GPU line. An absent GPU keeps its saved index.
    """
    job2 = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:a100:1 --wrap 'sleep infinity'",
        fatal=True,
    )
    assert not atf.wait_for_job_state(job2, "RUNNING", timeout=20), str(
        atf.get_job(job2)
    )
    assert atf.wait_for_job_state(job2, "PENDING"), str(atf.get_job(job2))
    job = str(atf.get_job(job_id))
    assert re.search(rf"gpu(?::a100)?:3\(IDX:{idx}\)", job), job
    assert atf.wait_for_job_state(job_id, "RUNNING"), "job should reach RUNNING"


def test_held_gpu_is_not_given_to_a_second_job():
    """drain_absent_gres on: an absent held GPU is drained in place.

    The node stays up, the job keeps all three GPUs, and no second job is
    given the absent one.
    """
    job_id = _hold_highest_gpu()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B))
    _wait_drained(_uuid_drained(GPU_C))
    assert atf.wait_for_job_state(job_id, "RUNNING"), str(atf.get_job(job_id))
    detail = atf.get_node_detail(NODE)
    assert re.search(r"GresUsed=gpu:(?:[^:]*:)?3\(IDX:0-2\)", detail), detail
    _second_job_pends(job_id)


def test_step_without_gres_runs_in_a_job_holding_an_absent_gpu():
    """A job that holds an absent GPU can still start a step without GRES.

    The job keeps the absent GPU, and the node stays up. A step given no
    GRES does not use that GPU, so it runs.
    """
    job_id = _hold_highest_gpu()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B))
    _wait_drained(_uuid_drained(GPU_C))
    assert atf.wait_for_job_state(job_id, "RUNNING"), str(atf.get_job(job_id))
    detail = atf.get_node_detail(NODE)
    assert re.search(r"GresUsed=gpu:(?:[^:]*:)?3\(IDX:0-2\)", detail), detail
    state = atf.get_node_field(NODE, "State") or ""
    assert not {"DOWN", "DRAIN", "INVAL"} & set(state.split("+")), detail

    atf.run_job(f"--jobid={job_id} --overlap --gres=none hostname", fatal=True)


def test_held_gpu_is_not_given_to_a_second_job_without_auto_drain(
    restore_drain_absent_gres,
):
    """The same refusal with drain_absent_gres off, the default.

    Reconfigure drops the token, as test_reconfigure_toggles_drain_absent_gres
    does, then a held GPU that disappears still leaves the node INVAL instead
    of shrinking the device bitmap out from under the job.
    """
    _reset()
    atf.require_config_parameter_excludes("SlurmctldParameters", "drain_absent_gres")
    _held_gpu_stays_allocated_across_reregistration()


def test_held_gpu_survives_slurmctld_restart():
    """A slurmctld restart while the held GPU is absent keeps the allocation.

    The absent GPU keeps its slot, drained, so the recovered allocation and
    GresUsed still name index 2.
    """
    job_id = _hold_highest_gpu()
    atf.restart_slurmd_with_fake_gpus(NODE, (0, GPU_A), (1, GPU_B))
    _wait_drained(_uuid_drained(GPU_C))

    atf.restart_slurmctld(clean=False, quiet=True)
    assert atf.wait_for_job_state(job_id, "RUNNING", timeout=60), str(
        atf.get_job(job_id)
    )
    for _ in atf.timer(timeout=60, fatal=True):
        _value = atf.get_node_detail(NODE)
        if re.search(r"GresUsed=gpu:(?:[^:]*:)?3\(IDX:0-2\)", _value or ""):
            break
    job = str(atf.get_job(job_id))
    assert re.search(r"gpu(?::a100)?:3\(IDX:0-2\)", job), job
    assert _uuid_drained(GPU_C)(atf.get_node_detail(NODE)), atf.get_node_detail(NODE)


def _wait_inval():
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State")
        if state is not None and "INVAL" in state:
            break


def _first_registration(*devices):
    """Register NODE with devices as the first registration a controller sees.

    Restarting slurmctld with clean state forgets the node's earlier device
    set. slurmd is stopped first, or it would register the old devices with
    the new controller, and the slurmd started after it makes the node's
    first registration.
    """
    atf.stop_slurmd(NODE)
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*devices))
    atf.restart_slurmctld(clean=True, quiet=True)
    atf.start_slurmd(NODE)


def test_absent_gpu_without_uuid_is_not_auto_drained():
    """A device that reports no UUID cannot be auto-drained.

    Nothing identifies which device is absent, so the node goes INVAL as
    without drain_absent_gres. The controller must never have seen UUIDs for
    these devices: losing them would read as every device going absent.
    """
    _first_registration((0, "(null)"), (1, "(null)"), (2, "(null)"))
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State")
        if state is not None and "INVAL" not in state and ("DOWN" not in state):
            break

    _register_slurmd((0, "(null)"), (2, "(null)"))
    _wait_inval()
    assert atf.get_node_tres_drained(NODE) is None, atf.get_node_detail(NODE)


def test_gpu_absent_at_first_registration_is_not_auto_drained():
    """A node short at its first registration has nothing to compare against.

    With no earlier device set, the controller cannot tell which device is
    absent, so the node goes INVAL instead and nothing is drained. The short
    report saves no order, so a registration with every device brings the
    node back and saves [A, B, C].
    """
    _first_registration((0, GPU_A), (2, GPU_C))
    _wait_inval()
    assert atf.get_node_tres_drained(NODE) is None, atf.get_node_detail(NODE)

    _register_slurmd((0, GPU_A), (1, GPU_B), (2, GPU_C))
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State")
        if state is not None and "INVAL" not in state:
            break
    assert atf.get_node_tres_drained(NODE) is None, atf.get_node_detail(NODE)

    atf.update_tres_drain(NODE, "gres/gpu", f"Uuid={GPU_B}", reason="b", fatal=True)
    assert atf.get_node_tres_drained(NODE) == "gpu(IDX:1)(b)", atf.get_node_detail(NODE)


@pytest.fixture
def short_count_only_gres():
    """Configure a count-only GRES of 4 on slurmctld while slurmd reports 2.

    slurmd reports the count its slurm.conf had when it started, since it
    makes up any count that gres.conf does not describe. So slurmd starts
    with 2, and only slurmctld is restarted with 4. The module's Gres is put
    back after the test.
    """
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3,shard:300,bandwidth:lustre:2")
    atf.stop_slurmctld(quiet=True)
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3,shard:300,bandwidth:lustre:4")
    atf.start_slurmctld(quiet=True)
    yield
    atf.stop_slurmd(NODE, quiet=True)
    atf.stop_slurmctld(quiet=True)
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3,shard:300")
    atf.start_slurmctld(quiet=True)
    atf.start_slurmd(NODE, fatal=True)


def test_short_count_only_gres_still_invalidates_the_node(short_count_only_gres):
    """drain_absent_gres covers only devices identified by UUID.

    A count-only GRES has no device to drain, so a registration with fewer
    of it than configured still marks the node INVAL.
    """
    _wait_inval()
    detail = atf.get_node_detail(NODE)
    assert "gres/bandwidth count reported lower than configured" in detail, detail


@pytest.fixture
def drainable_count_only_gres():
    """Configure a count-only GRES of 4 on the node, as gres.conf has it.

    The module's gres.conf and Gres are put back after the test, which also
    starts the node's slurmd again.
    """
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C))
    )
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        f"NodeName={NODE} Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n",
    )
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3,shard:300,bandwidth:lustre:4")
    yield
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3,shard:300")


def test_lowering_a_drained_count_only_gres_clears_degraded(
    drainable_count_only_gres,
):
    """A restart that configures a drained count-only GRES away clears DEGRADED.

    The drain is cut to the new configured count of 0, so nothing is left
    drained. slurmctld starts while the node's slurmd is stopped, so it is
    the restart, not a registration, that has to clear DEGRADED, and sinfo -R
    must not list the node.
    """
    _reset()
    atf.update_tres_drain(
        NODE, "gres/bandwidth:lustre", "Count=2", reason="bw", fatal=True
    )
    assert atf.wait_for_node_state(NODE, "DEGRADED"), atf.get_node_detail(NODE)

    atf.stop_slurmd(NODE, quiet=True)
    atf.stop_slurmctld(quiet=True)
    atf.set_node_parameter(NODE, "Gres", "gpu:a100:3,shard:300,bandwidth:lustre:0")
    atf.start_slurmctld(quiet=True)

    assert atf.get_node_tres_drained(NODE) is None, atf.get_node_detail(NODE)
    state = atf.get_node_field(NODE, "State") or ""
    assert "DEGRADED" not in state, atf.get_node_detail(NODE)
    output = atf.run_command_output(
        f"sinfo -R -h -n {NODE} -o '%N'",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    assert NODE not in output.split(), output
