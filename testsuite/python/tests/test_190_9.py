############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Accounting event coverage for per-TRES drain and undrain.

Draining a node-local TRES -- GRES, CPU, or memory -- on a node (leaving the
node DEGRADED but usable) must open an event in the slurmdbd event_table whose
TRES column reflects the drained resources; undraining must close that event.
"""

import re
import time

import pytest

import atf

pytestmark = pytest.mark.slow

# Node names need to be in the form of nodeNN for the s2n variant. DYN_NODE is
# the node under test; DYN_NODE_GOOD is a second, healthy node that joins the
# cluster in the test for an absent node's drain event.
DYN_NODE = "node100"
DYN_NODE_GOOD = "node101"

# Each dynamic slurmd on the same host must listen on a distinct port. Without
# explicit Port=, both slurmds default to SlurmdPort (e.g. 6818) and only the
# first one can bind; the second node fails to register.
PORT_GOOD = 61601

# Fake GPU device UUIDs, in fake_gpus.conf device order.
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"
# An old-style MIG UUID, with "/" separators.
GPU_MIG = "MIG-GPU-dddddddd-3333-3333-3333-333333333333/1/0"
# Its parentheses cannot be written into a TresReference, so a drain of this
# device alone records an event with a blank one.
GPU_UNRECORDABLE = "GPU-eeeeeeee-4444-4444-4444-(444444444444)"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MaxNodeCount", 4)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "bandwidth")
    atf.require_accounting()
    # get_node_events() queries the TresReference event format field, which only
    # exists in 26.11 sacctmgr; an older sacctmgr exits 1 on an unknown
    # field.
    atf.require_version(
        (26, 11),
        component="bin/sacctmgr",
        reason="Ticket 25180: TresReference event format field and per-TRES drain accounting "
        "events added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots and MaxDrainedTres added in 26.11",
    )
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/bandwidth")
    atf.require_config_parameter_includes(
        "AccountingStorageTRES", "gres/bandwidth:lustre"
    )
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_slurm_running()


def _wait_for_next_second():
    """Wait until the clock has moved into a later second.

    Event rows are keyed on their start second, so a refresh in the same second
    rewrites the row in place instead of starting a new one.
    """
    start = int(time.time())
    for _ in atf.timer(timeout=3, poll_interval=0.05, fatal=True, quiet=True):
        if int(time.time()) > start:
            break


@pytest.fixture
def two_gpu_dynamic_node(dynamic_slurmds):
    """Configure the dynamic node with two a100 GPUs, restart Slurm, start it.

    Yields a callable returning the started node name. ``gres_conf`` writes
    gres.conf when the test needs one of its own (see atf.gpu_gres_conf());
    ``uuids`` gives the two devices reportable UUIDs.
    """

    def _start(
        extra_conf="Gres=gpu:a100:2",
        node_name=DYN_NODE,
        gres_conf=None,
        uuids=(None, None),
    ):
        if gres_conf is not None:
            atf.require_config_file("gres.conf", gres_conf)
        atf.require_config_file(
            "fake_gpus.conf",
            atf.fake_gpus_conf(
                (0, uuids[0] or "(null)"),
                (1, uuids[1] or "(null)"),
            ),
        )
        atf.restart_slurm(clean=True, quiet=True)
        return dynamic_slurmds(node_name, extra_conf)

    return _start


@pytest.fixture
def maint_resv():
    """Name a maintenance reservation the test creates, and delete it after.

    A dynamic node in a reservation cannot be deleted, and neither can one
    left in MAINTENANCE. Deleting a reservation the test already deleted only
    fails, quietly. When it is still there, wait for MAINTENANCE to clear
    before the dynamic node is deleted.
    """
    resv = "resv_190_9_maint"
    yield resv
    slurm_user = atf.properties["slurm-user"]
    show = (
        atf.run_command_output(
            f"scontrol show reservation {resv}",
            user=slurm_user,
            fatal=False,
            quiet=True,
        )
        or ""
    )
    nodes = []
    match = re.search(r"\bNodes=(\S+)", show)
    if match and match.group(1) != "(null)":
        nodes = match.group(1).split(",")
    atf.run_command(
        f"scontrol delete reservationname={resv}",
        user=slurm_user,
        quiet=True,
    )
    for node in nodes:
        atf.wait_for_node_state(node, "MAINTENANCE", reverse=True, fatal=False)


def _has_open_drain_event(node, reason):
    """True if there is an open event for node with the given reason and a gpu TRES."""
    out = atf.get_node_events(node) or ""
    # An open event has no end time, which sacctmgr prints as "Unknown". The
    # State column carries the node's state when the event opened, which for a
    # node with TRES drained but otherwise in service reads DEG:
    # node|reason|gres/gpu=1|gres/gpu:a100(IDX:0)(UUID:GPU-...)|Unknown|DEG|2026-...
    return bool(
        re.search(
            rf"^{node}\|{reason}\|[^|]*gpu[^|]*\|[^|]*\|Unknown\|",
            out,
            re.MULTILINE,
        )
    )


def _drain_events(node, before=()):
    """The node's partial-drain events as lists of fields, open or closed.

    Events already in before are left out. The node name is reused from test to
    test, so its earlier events are still in the table.

    A partial drain is the event with a TresReference, which a whole-node event
    leaves blank. The fields are those of atf.get_node_events():
    nodename|reason|tres|tresreference|end|state|start
    """
    return [
        line.split("|")
        for line in (atf.get_node_events(node) or "").splitlines()
        if line.count("|") >= 6 and line.split("|")[3] and line.split("|") not in before
    ]


def _events_with_reason(node, reason):
    """The node's events whose Reason column is exactly ``reason``.

    Fields are those of atf.get_node_events():
    nodename|reason|tres|tresreference|end|state|start
    """
    rows = []
    for line in (atf.get_node_events(node) or "").splitlines():
        parts = line.split("|")
        if len(parts) >= 7 and parts[0] == node and parts[1] == reason:
            rows.append(parts)
    return rows


def _setup_uuid_gpus(*devices):
    """Configure the node with (tty, type, uuid) GPUs and restart Slurm."""
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        + "".join(
            f"NodeName={DYN_NODE} Name=gpu File=/dev/tty{tty} Type={gpu_type}\n"
            for tty, gpu_type, _ in devices
        ),
    )
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*devices))
    atf.restart_slurm(clean=True, quiet=True)


def test_tres_drain_multiple_gres_records_combined_event(two_gpu_dynamic_node):
    """Several GRES drained for one reason share one event listing them all.

    An event covers the TRES drained for a single reason, so draining more
    GRES with the reason already open updates that event in place rather than
    leaving a stale single-GRES event or opening a second one.
    """
    node = two_gpu_dynamic_node(
        extra_conf="Gres=gpu:a100:2,bandwidth:lustre:4",
        gres_conf=(
            atf.gpu_gres_conf(DYN_NODE) + f"NodeName={DYN_NODE} AutoDetect=off "
            f"Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n"
        ),
    )

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="combo")
    assert result["exit_code"] == 0, result
    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=2", reason="combo")
    assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if (
            t
            and re.search("(?:^|,)gres/gpu=1(?:,|$)", t)
            and re.search("(?:^|,)gres/bandwidth=2(?:,|$)", t)
            and re.search("(?:^|,)gres/bandwidth:lustre=2(?:,|$)", t)
        ):
            break
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"the drains should stay one event: {atf.get_node_events(node)!r}"


def test_tres_drain_same_reason_updates_event(two_gpu_dynamic_node):
    """A second drain with the same reason refreshes the event's TRES.

    Event dedup compares the TRES column too, so draining more of the same GRES
    (even with an identical reason) updates the open event rather than being
    treated as 'no change'.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="same")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and re.search("gpu=1\\b", t):
            break
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"the first drain should be one event: {atf.get_node_events(node)!r}"

    result = atf.update_tres_drain(node, "gres/gpu", "Index=1", reason="same")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and re.search("gpu=2\\b", t):
            break
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"the second drain should stay one event: {atf.get_node_events(node)!r}"


def test_tres_partial_resume_updates_event(two_gpu_dynamic_node):
    """Resuming part of a drained GRES keeps the event open with the remainder.

    Draining two GPUs then resuming one must leave the node DEGRADED with the
    open event's TRES reduced to the still-drained GPU (gres/gpu=1) rather than
    closing the event or leaving a stale gres/gpu=2.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0,1", reason="partresume")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("gpu=2\\b", t)):
            break

    # Resume only one GPU; the node stays DEGRADED and the event shrinks to =1.
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", action="Resume")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("gpu=1\\b", t)):
            break
    assert "DEGRADED" in (
        atf.get_node_parameter(node, "state") or ""
    ), f"node should still be DEGRADED after partial resume: {atf.get_node_events(node)!r}"


def test_whole_node_drain_supersedes_drained_tres_event(two_gpu_dynamic_node):
    """A whole-node DRAIN replaces the node's drained-TRES event.

    A node has one event open at a time. The whole-node event accounts for
    every TRES on the node, the drained GPU included, so it closes the
    drained-TRES event rather than running alongside it -- charging both would
    count the GPU twice. Resuming the node hands the row back: the GPU is
    still drained, so a drained-TRES event opens again.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    # Drain one GPU: node DEGRADED with an open GPU-subset event.
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="partdrain")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "partdrain")
        if found:
            break

    # Drain the whole node: its event takes over the node's one open row.
    result = atf.run_command(
        f"scontrol update NodeName={node} State=DRAIN Reason=wholenode",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        reason = atf.get_open_node_event_reason(node)
        if reason == "wholenode":
            break

    # It covers the whole node, so it carries node-level TRES.
    tres = atf.get_open_node_event_tres(node)
    assert tres and re.search(
        r"\bcpu=", tres
    ), f"whole-node event should cover the node's TRES: {atf.get_node_events(node)!r}"
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected only the whole-node event open: {atf.get_node_events(node)!r}"
    assert atf.get_open_node_event_tres_reference(node) == "", (
        "whole-node event must store an empty TresReference, not a placeholder: "
        f"{atf.get_node_events(node)!r}"
    )

    # Resume the node, keeping its drained TRES -- plain State=RESUME would
    # resume the GPU too, and this is about the events. The GPU is still
    # drained, so a drained-TRES event opens again, carrying the reason the GPU
    # was drained with rather than the node's cleared one.
    result = atf.run_command(
        f"scontrol update NodeName={node} State=RESUME_KEEP_TRES",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        reason = atf.get_open_node_event_reason(node)
        if reason == "partdrain":
            break

    tres = atf.get_open_node_event_tres(node)
    assert tres and re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", tres
    ), f"reopened event should describe the drained GPU: {atf.get_node_events(node)!r}"
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected a single open event: {atf.get_node_events(node)!r}"
    assert not atf.get_overlapping_node_events(
        node
    ), f"events overlap each other: {atf.get_node_events(node)!r}"


def test_max_drained_tres_escalation_opens_whole_node_event(two_gpu_dynamic_node):
    """Exceeding MaxDrainedTres auto-drains the node and opens its own event.

    Under a MaxDrainedTres=gres/gpu:1 limit, draining one GPU leaves the node
    DEGRADED with an open GPU-subset event. Draining the second GPU exceeds the
    limit, so the controller drains the whole node. A node has one event open
    at a time, so the whole-node event replaces the drained-TRES one -- it
    covers every TRES on the node, the drained GPUs included, so nothing goes
    unaccounted.
    """
    node = two_gpu_dynamic_node(
        extra_conf="Gres=gpu:a100:2 MaxDrainedTres=gres/gpu:1",
        gres_conf=atf.gpu_gres_conf(DYN_NODE),
    )

    # Drain one GPU: within the limit, so only DEGRADED with an open event.
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="underlimit")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "underlimit")
        if found:
            break
    assert "DRAIN" not in (
        atf.get_node_parameter(node, "state") or ""
    ), f"node should not be DRAINED at the limit: {atf.get_node_events(node)!r}"

    # Drain the second GPU: exceeds MaxDrainedTres, so the node is drained and
    # the whole-node event takes over the node's one open row, in the same
    # second the drained-TRES event would have been rewritten in.
    result = atf.update_tres_drain(node, "gres/gpu", "Index=1", reason="overlimit")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        node, "DRAIN"
    ), f"node was not drained after exceeding MaxDrainedTres: {atf.get_node_events(node)!r}"
    assert (
        atf.get_node_parameter(node, "reason")
        == "gpu has 2 drained, exceeding MaxDrainedTres limit of 1"
    ), atf.get_node_parameter(node, "reason")
    # The whole-node event covers node-level resources (cpu, mem), not just the
    # GPU subset.
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("\\bcpu=", t)) and bool(re.search("\\bmem=", t)):
            break

    assert atf.get_open_node_event_tres_reference(node) == "", (
        "whole-node event must store an empty TresReference, not a placeholder: "
        f"{atf.get_node_events(node)!r}"
    )

    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected only the whole-node event open: {atf.get_node_events(node)!r}"
    assert not atf.get_overlapping_node_events(
        node
    ), f"events overlap each other: {atf.get_node_events(node)!r}"


def test_tres_drain_cpu_mem_undrain_records_accounting_event(dynamic_slurmds):
    """Node-local CPU and memory drains open one event; undraining closes it.

    CPU and memory are node-local TRES like GRES, so draining them on an
    otherwise-usable node must open an event_table interval whose TRES column
    reflects the drained cpu/mem. Resuming both must close the interval.
    """
    # No GRES: this test exercises node-local cpu/mem drain accounting only.
    atf.require_config_file("gres.conf", "")
    atf.require_config_file("fake_gpus.conf", "")
    atf.restart_slurm(clean=True, quiet=True)

    node = dynamic_slurmds(DYN_NODE)

    # Drain a CPU and some memory; the node stays usable (DEGRADED).
    result = atf.update_tres_drain(node, "cpu", "Index=0", reason="nl_evt")
    assert result["exit_code"] == 0, result
    result = atf.update_tres_drain(node, "mem", "Count=512", reason="nl_evt")
    assert result["exit_code"] == 0, result

    # A single open event should list both node-local TRES.
    for _ in atf.timer(fatal=True):
        ev = atf.get_open_node_event(node)
        if (
            ev
            and ev[0] == "nl_evt"
            and re.search("(?:^|,)cpu=[1-9]", ev[1] or "")
            and re.search("(?:^|,)mem=512M?(?:,|$)", ev[1] or "")
        ):
            break

    # Resume both; the open event interval must close.
    for tres, selector in (("cpu", "Index=0"), ("mem", "Count=512")):
        result = atf.update_tres_drain(node, tres, selector, action="Resume")
        assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        ev = atf.get_open_node_event(node)
        if ev is None:
            break


def test_cpu_mem_reason_only_updates_refresh_accounting(dynamic_slurmds):
    """Re-draining the same CPU or memory count replaces the event reason.

    Action=SetDrain makes the second request resolve to the set already
    drained. Action=Drain would add to it, which is a different (drained-more)
    update and would not exercise the reason-only path.
    """
    atf.require_config_file("gres.conf", "")
    atf.require_config_file("fake_gpus.conf", "")
    atf.restart_slurm(clean=True, quiet=True)
    node = dynamic_slurmds(DYN_NODE)

    for tres, selector in (("cpu", "Index=0"), ("mem", "Count=512")):
        atf.update_tres_drain(node, tres, selector, reason="reason_old", fatal=True)
        for _ in atf.timer(fatal=True):
            reason = atf.get_open_node_event_reason(node)
            if reason == "reason_old":
                break

        atf.update_tres_drain(
            node,
            tres,
            selector,
            action="SetDrain",
            reason="reason_new",
            fatal=True,
        )
        for _ in atf.timer(fatal=True):
            reason = atf.get_open_node_event_reason(node)
            if reason == "reason_new":
                break

        atf.update_tres_drain(node, tres, selector, action="Resume", fatal=True)
        for _ in atf.timer(fatal=True):
            reason = atf.get_open_node_event_reason(node)
            if reason is None:
                break


def test_tres_drain_gres_cpu_mem_records_combined_event(two_gpu_dynamic_node):
    """Draining GRES with node-local CPU/mem keeps one combined event.

    The TRES-aware event dedup must fold node-local cpu/mem drains into the same
    open event as the GRES drain, so the event stays a complete snapshot of every
    drained TRES on the node rather than several partial events.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    for tres, selector in (
        ("gres/gpu", "Index=0"),
        ("cpu", "Index=0"),
        ("mem", "Count=512"),
    ):
        result = atf.update_tres_drain(node, tres, selector, reason="combo")
        assert result["exit_code"] == 0, result

    # One open event whose TRES lists the GPU together with cpu and mem.
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if (
            t
            and re.search("(?:^|,)gres/gpu=1(?:,|$)", t)
            and re.search("(?:^|,)cpu=[1-9]", t)
            and re.search("(?:^|,)mem=512M?(?:,|$)", t)
        ):
            break
    assert atf.get_open_node_event_tres_reference(node) == (
        "cpu(IDX:0),gres/gpu:a100(IDX:0),mem(CNT:512)"
    ), f"combined TresReference is wrong: {atf.get_node_events(node)!r}"


def test_drain_records_gpu_uuid_in_event(dynamic_slurmds):
    """A drained GPU's UUID is recorded in the event's TresReference column."""
    _setup_uuid_gpus((0, "a100", GPU_A), (1, "a100", GPU_B))
    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:2")

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="uuid_evt")
    assert result["exit_code"] == 0, result

    # The identity is fully qualified by GRES name and type, and -P must not
    # truncate it.
    for _ in atf.timer(fatal=True):
        u = atf.get_open_node_event_tres_reference(node)
        if u == f"gres/gpu:a100(IDX:0)(UUID:{GPU_A})":
            break

    # The count column stays a plain TRES count alongside it.
    assert re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", atf.get_open_node_event_tres(node) or ""
    ), f"open event TRES count is wrong: {atf.get_node_events(node)!r}"


def test_swapping_drained_gpu_updates_event_uuid(dynamic_slurmds):
    """Swapping which GPU is drained must not leave the event naming the old one.

    Resuming one GPU and draining another in a single update leaves the node
    state, the reason and the drained TRES counts identical -- only the device
    identity changes. The event dedup must compare the UUIDs too, or the open
    event keeps naming the wrong physical GPU.
    """
    _setup_uuid_gpus((0, "a100", GPU_A), (1, "a100", GPU_B))
    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:2")

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="swap")
    assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        u = atf.get_open_node_event_tres_reference(node)
        if u == f"gres/gpu:a100(IDX:0)(UUID:{GPU_A})":
            break

    # event_table's primary key is (node_name, time_start) at one-second
    # granularity. Sleep past it so the swap lands on a new time_start and the
    # GPU_A interval is closed and a new row inserted, rather than the existing
    # row being refreshed in place -- which is what keeps GPU_A's interval in
    # the history asserted below.
    _wait_for_next_second()

    # Action=SetDrain makes the drained set exactly this selection, so it
    # resumes the first GPU and drains the second in one RPC. Count stays 1 and
    # the reason is unchanged, so only the UUID distinguishes the two states.
    result = atf.update_tres_drain(
        node, "gres/gpu", "Index=1", action="SetDrain", reason="swap"
    )
    assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        u = atf.get_open_node_event_tres_reference(node)
        if u == f"gres/gpu:a100(IDX:1)(UUID:{GPU_B})":
            break

    # The count is unchanged, confirming the UUID alone drove the update.
    assert re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", atf.get_open_node_event_tres(node) or ""
    ), f"open event TRES count changed unexpectedly: {atf.get_node_events(node)!r}"

    # GPU_A's drain interval is preserved as history.
    assert GPU_A in (
        atf.get_node_events(node) or ""
    ), f"history lost the GPU_A drain interval: {atf.get_node_events(node)!r}"


def test_typed_gres_uuid_keys_are_distinct(dynamic_slurmds):
    """Devices of different types get distinct, sorted, fully qualified keys.

    h100 is placed at the lower device index so bitmap order and lexicographic
    order disagree; the expected string is therefore only produced if the
    entries really are sorted. The event dedup compares this string verbatim,
    so a non-canonical ordering would spuriously reopen events.
    """
    _setup_uuid_gpus((0, "h100", GPU_C), (1, "a100", GPU_A))
    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:h100:1,gpu:a100:1")

    result = atf.update_tres_drain(node, "gres/gpu", "Index=ALL", reason="typed")
    assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        u = atf.get_open_node_event_tres_reference(node)
        if u == (
            f"gres/gpu:a100(IDX:1)(UUID:{GPU_A})," f"gres/gpu:h100(IDX:0)(UUID:{GPU_C})"
        ):
            break


def test_gres_without_uuid_records_index_in_tres_reference(two_gpu_dynamic_node):
    """A UUID-less device is identified by index in TresReference."""
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="no_uuid")
    assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        u = atf.get_open_node_event_tres_reference(node)
        if u == "gres/gpu:a100(IDX:0)":
            break


def test_cpu_mem_and_count_only_drain_are_named_in_tres_reference(dynamic_slurmds):
    """CPU, memory and count-only GRES are recorded with IDX/CNT groups."""
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        f"NodeName={DYN_NODE} Name=gpu File=/dev/tty0 Type=a100\n"
        f"NodeName={DYN_NODE} AutoDetect=off "
        f"Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n",
    )
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A)))
    atf.restart_slurm(clean=True, quiet=True)

    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:1,bandwidth:lustre:4")

    for tres, selector in (
        ("cpu", "Index=0"),
        ("mem", "Count=512"),
        ("gres/bandwidth", "Count=2"),
    ):
        result = atf.update_tres_drain(node, tres, selector, reason="no_ident")
        assert result["exit_code"] == 0, result

    for _ in atf.timer(fatal=True):
        u = atf.get_open_node_event_tres_reference(node)
        if u == "cpu(IDX:0),gres/bandwidth:lustre(CNT:2),mem(CNT:512)":
            break


def test_rapid_drain_resume_keeps_one_open_event(two_gpu_dynamic_node):
    """Drain/resume churn on a degraded node must not strand an open event.

    The event table is keyed on (node_name, time_start), so two events for one
    node landing in the same second collide. Reopening the colliding row
    without replacing what it holds resurrects whatever it described before --
    typically a whole-node event carrying the node's full TRES, left open with
    no end, which the rollup then charges against every TRES on the node while
    jobs keep running there.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="churn", fatal=True)
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "churn")
        if found:
            break

    # Same-second collisions are the point, so do not pause between these.
    for _ in range(10):
        atf.run_command(
            f"scontrol update NodeName={node} State=DRAIN Reason=churn_node",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        # RESUME_KEEP_TRES, not RESUME: the churn is meant to reopen the
        # whole-node event against a node whose GPU stays drained throughout.
        atf.run_command(
            f"scontrol update NodeName={node} State=RESUME_KEEP_TRES",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )

    # The GPU is still drained, so the node settles back to DEGRADED with its
    # drained-TRES event open -- and only that one.
    for _ in atf.timer(fatal=True):
        state = atf.get_node_parameter(node, "state") or ""
        if "DEGRADED" in state and "DRAIN" not in state:
            break

    # Without this the pre-churn row already satisfies every check below.
    atf.wait_for_dbd_agent_drained(fatal=True)

    for _ in atf.timer(fatal=True):
        count = atf.get_open_node_event_count(node)
        if count == 1:
            break

    tres = atf.get_open_node_event_tres(node)
    assert tres and re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", tres
    ), f"open event should describe the drained GPU: {atf.get_node_events(node)!r}"
    assert not re.search(
        r"(?:^|,)cpu=", tres
    ), f"a whole-node event was resurrected: {atf.get_node_events(node)!r}"
    assert not atf.get_overlapping_node_events(
        node
    ), f"events overlap each other: {atf.get_node_events(node)!r}"


def test_drain_with_running_job_keeps_partial_event_live(two_gpu_dynamic_node):
    """A node draining with jobs on it keeps its drained-TRES event live.

    No whole-node event is sent until the last job exits, so the drained-TRES
    event is still the one describing what is unavailable and has to stay
    refreshable. It must not freeze the moment the node becomes DRAIN: further
    drains must be recorded, and resuming every drained TRES must close it.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    holder = atf.submit_job_sbatch(f"-w {node} --wrap 'sleep infinity'", fatal=True)
    atf.wait_for_job_state(holder, "RUNNING", fatal=True, timeout=60)

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="live0", fatal=True)
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("(?:^|,)gres/gpu=1(?:,|$)", t)):
            break

    # Drain the node itself. The job is still running, so no whole-node
    # event is sent and the drained-TRES event is still the live one.
    atf.run_command(
        f"scontrol update NodeName={node} State=DRAIN Reason=drainjob",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.wait_for_node_state(
        node, "DRAIN"
    ), f"node was not drained: {atf.get_node_events(node)!r}"

    # Draining a second GPU must still be recorded. It joins the node's
    # one event, so the counts grow and the reason gains the new one.
    atf.update_tres_drain(node, "gres/gpu", "Index=1", reason="live1", fatal=True)
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("(?:^|,)gres/gpu=2(?:,|$)", t)):
            break
    assert (
        atf.get_open_node_event_reason(node) == "live0,live1"
    ), f"both reasons should be recorded: {atf.get_node_events(node)!r}"
    tres = atf.get_open_node_event_tres(node)
    assert not re.search(
        r"(?:^|,)cpu=", tres or ""
    ), f"a whole-node event opened while jobs were running: {atf.get_node_events(node)!r}"
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected a single open event: {atf.get_node_events(node)!r}"
    atf.cancel_jobs([holder], fatal=True)

    # With the job gone the node is DRAINED, and the whole-node event (which
    # carries the node's full TRES) supersedes the drained-TRES one.
    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("(?:^|,)cpu=", t)):
            break
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected exactly one open whole-node event: {atf.get_node_events(node)!r}"
    assert not atf.get_overlapping_node_events(
        node
    ), f"events overlap each other: {atf.get_node_events(node)!r}"


def test_node_update_preserves_open_degraded_event(two_gpu_dynamic_node):
    """Updating a node's instance details leaves its drained-TRES event alone.

    A node whose only event is an open one must not count as never seen.
    Otherwise the node-update path synthesizes a whole-node down/up pair that
    closes the drained-TRES interval.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="instupd", fatal=True)
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "instupd")
        if found:
            break

    atf.run_command(
        f"scontrol update NodeName={node} InstanceId=i-165-9 InstanceType=t-165-9",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Nothing about the node's drained TRES changed, so neither should its event.
    for _ in atf.timer(fatal=True):
        event = atf.get_open_node_event(node)
        if (
            bool(event)
            and event[0] == "instupd"
            and bool(re.search("(?:^|,)gres/gpu=1(?:,|$)", event[1]))
        ):
            break
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected exactly one open drained-TRES event: {atf.get_node_events(node)!r}"


def test_quoted_drain_reason_stays_one_event(dynamic_slurmds):
    """A drain reason containing a quote and a backslash stays one event.

    slurmdbd unpacks the reason with quotes and backslashes escaped, then
    stores the text MariaDB keeps after interpreting those escapes. Resending
    the same drain compares the unpacked reason to that stored text. If they
    differ, the open interval is closed and a new one is opened.

    The resend is a node Reason update. That leaves the drained CPU and its
    reason alone, and update_node reports the drain again because the node's
    own reason changed. An InstanceId update does not: an unchanged node
    reason skips the accounting update. Sleeping past the event's start
    second means a failed compare inserts a second row rather than updating
    the first in place.
    """
    # The quote and the backslash are the characters slurmdbd escapes.
    reason = f"it's\\a-{time.time_ns()}"

    atf.require_config_file("gres.conf", "")
    atf.require_config_file("fake_gpus.conf", "")
    atf.restart_slurm(clean=True, quiet=True)
    node = dynamic_slurmds(DYN_NODE)

    result = atf.update_tres_drain(node, "cpu", "Index=0", reason=reason)
    assert result["exit_code"] == 0, result

    drained = atf.get_node_tres_drained(node) or ""
    assert (
        "'" in drained and "\\" in drained
    ), f"the drain reason lost its quote or backslash: {drained!r}"
    assert "DEGRADED" in (atf.get_node_parameter(node, "state") or ""), (
        f"node is not DEGRADED, so a later update would not resend the drain: "
        f"{atf.get_node_parameter(node, 'state')!r}"
    )

    for _ in atf.timer(fatal=True):
        rows = _events_with_reason(node, reason)
        if len(rows) == 1 and rows[0][4].strip() == "Unknown":
            break

    start = _events_with_reason(node, reason)[0][6].strip()
    # event_table's primary key is (node_name, time_start) at one-second
    # granularity. A resend in this same second updates the row in place
    # whether or not the reason compared equal, which would hide a mismatch.
    _wait_for_next_second()

    atf.run_command(
        f"scontrol update NodeName={node} Reason=resend",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_dbd_agent_drained(fatal=True)

    rows = _events_with_reason(node, reason)
    assert len(rows) == 1, (
        "resending the drain opened another event for the same reason: "
        f"{atf.get_node_events(node)!r}"
    )
    assert (
        rows[0][4].strip() == "Unknown"
    ), f"the original event was closed: {atf.get_node_events(node)!r}"
    assert rows[0][6].strip() == start, (
        "the open event was replaced: "
        f"start {start!r} became {rows[0][6]!r}; "
        f"{atf.get_node_events(node)!r}"
    )


def test_drain_event_stays_one_row_as_node_state_changes(two_gpu_dynamic_node):
    """One continuous drain is one event, whatever state the node moves through.

    A node going from idle to allocated with its drain unchanged refreshes the
    open drained-TRES interval. It does not close it and open an identical
    one, so the drain stays a single row.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))
    before = _drain_events(node)

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="onedrain", fatal=True)
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "onedrain")
        if found:
            break
    events = _drain_events(node, before)
    assert len(events) == 1, f"expected one drain event: {events!r}"

    # A job with no GRES leaves the drain alone but changes the node's state.
    job_id = atf.submit_job_sbatch(f"-w {node} --wrap 'sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)

    # Rows are keyed on their start second. A refresh in that same second
    # rewrites the row in place, so let a second pass first.
    _wait_for_next_second()

    # Refreshes the node's event with nothing about its drain changed.
    atf.run_command(
        f"scontrol update NodeName={node} Reason=refresh",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # A split shows up as a second row. Expect it not to.
    atf.wait_for_dbd_agent_drained(fatal=True)
    got = _drain_events(node, before)
    assert len(got) <= 1, f"the drain was split into several events: {got!r}"
    assert got == events, f"drain event changed: {got!r}"


def test_drain_event_without_reference_stays_one_row(dynamic_slurmds):
    """A drain event with a blank TresReference is still one row across states.

    A drained device whose identity cannot be written to TresReference leaves
    it blank. The dedup must still tell its partial drain from a whole-node
    event, which it does by the DEGRADED state that only a partial drain
    records, or the event is split on every state change.
    """
    _setup_uuid_gpus((0, "a100", GPU_UNRECORDABLE), (1, "a100", GPU_B))
    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:2")

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="norefdrain", fatal=True)
    for _ in atf.timer(fatal=True):
        rows = _events_with_reason(node, "norefdrain")
        if len(rows) == 1 and rows[0][4] == "Unknown":
            break
    events = _events_with_reason(node, "norefdrain")

    # The case under test: the event names no device.
    assert events[0][3] == "", f"expected a blank TresReference: {events!r}"

    # A job with no GRES leaves the drain alone but changes the node's state.
    job_id = atf.submit_job_sbatch(f"-w {node} --wrap 'sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)

    # Rows are keyed on their start second. A refresh in that same second
    # rewrites the row in place, so let a second pass first.
    _wait_for_next_second()

    # Refreshes the node's event with nothing about its drain changed.
    atf.run_command(
        f"scontrol update NodeName={node} Reason=refresh",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # A split shows up as a second row. Expect it not to.
    atf.wait_for_dbd_agent_drained(fatal=True)
    got = _events_with_reason(node, "norefdrain")
    assert len(got) <= 1, f"the drain was split into several events: {got!r}"
    assert got == events, f"drain event changed: {got!r}"


def test_legacy_mig_uuid_is_recorded(dynamic_slurmds):
    """A legacy MIG UUID, with "/" separators, is named in TresReference."""
    _setup_uuid_gpus((0, "a100", GPU_MIG), (1, "a100", GPU_B))
    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:2")

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="migdrain", fatal=True)
    for _ in atf.timer(fatal=True):
        rows = _events_with_reason(node, "migdrain")
        if len(rows) == 1 and f"(UUID:{GPU_MIG})" in rows[0][3]:
            break


def test_drain_event_follows_maintenance_state(two_gpu_dynamic_node, maint_resv):
    """A drain event is split when the node enters and leaves maintenance.

    The rollup leaves event rows with the MAINT state to the reservation's
    accounting. A drain event that kept the state it opened with would stay
    counted as down through the maintenance, or never be counted if it was
    opened during it. Starting and ending the reservation each start a new row
    on their own, and the drain itself is still one continuous drain.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))
    slurm_user = atf.properties["slurm-user"]
    resv = maint_resv
    before = _drain_events(node)

    def _is_open(event):
        return event[4] == "Unknown"

    def _is_maint(event):
        return "MAINT" in event[5].upper()

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="maintdrain", fatal=True)
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "maintdrain")
        if found:
            break
    events = _drain_events(node, before)
    assert len(events) == 1, f"expected one drain event: {events!r}"
    assert not _is_maint(events[0]), f"drain event opened as MAINT: {events!r}"

    # Rows are keyed on their start second, so let one pass.
    _wait_for_next_second()

    atf.run_command(
        f"scontrol create reservationname={resv} start=now duration=60 "
        f"nodes={node} user={slurm_user} flags=maint,ignore_jobs",
        user=slurm_user,
        fatal=True,
    )
    assert atf.wait_for_node_state(
        node, "MAINTENANCE"
    ), f"{node} did not enter maintenance: {atf.get_node_events(node)!r}"

    # Nothing else touches the node: the reservation starting must split
    # the event by itself.
    for _ in atf.timer(fatal=True):
        events = _drain_events(node, before)
        if len(events) == 2 and any((_is_open(e) and _is_maint(e) for e in events)):
            break

    _wait_for_next_second()

    atf.run_command(
        f"scontrol delete reservationname={resv}", user=slurm_user, fatal=True
    )
    assert atf.wait_for_node_state(
        node, "MAINTENANCE", reverse=True
    ), f"{node} did not leave maintenance: {atf.get_node_events(node)!r}"

    for _ in atf.timer(fatal=True):
        events = _drain_events(node, before)
        if len(events) == 3 and any(
            (_is_open(e) and (not _is_maint(e)) for e in events)
        ):
            break

    # One continuous drain: exactly one row is open, and every row records the
    # same drained GPU with the same reason.
    events = _drain_events(node, before)
    assert (
        sum(_is_open(e) for e in events) == 1
    ), f"expected exactly one open drain event: {events!r}"
    assert (
        len({(e[1], e[2], e[3]) for e in events}) == 1
    ), f"drain changed across the maintenance: {events!r}"


def test_degraded_events_selectable_by_state(two_gpu_dynamic_node):
    """sacctmgr selects events by the node's own state.

    A partial TRES drain is DEGRADED, so States=Drain does not match it.
    A whole-node drain of that node is selected by States=Drain and not by
    States=Degraded.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="bystate", fatal=True)
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "bystate")
        if found:
            break

    def _events_in_state(state):
        # all_time, not "start=": a start date makes sacctmgr ask for
        # "time_start < now", which hides the event just opened above until the
        # clock ticks past the second it was opened in.
        return atf.run_command_output(
            f"sacctmgr -n -P list events all_time Event=Node Nodes={node} "
            f"States={state} format=nodename,reason,tres,end",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )

    assert "bystate" in (
        _events_in_state("Degraded") or ""
    ), f"States=Degraded did not select the drained-TRES event: {atf.get_node_events(node)!r}"
    assert "bystate" not in (
        _events_in_state("Drain") or ""
    ), f"States=Drain should not match a drained-TRES event: {atf.get_node_events(node)!r}"

    atf.run_command(
        f"scontrol update NodeName={node} State=DRAIN Reason=outright",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        reason = atf.get_open_node_event_reason(node)
        if reason == "outright":
            break
    assert "outright" in (
        _events_in_state("Drain") or ""
    ), f"States=Drain missed the whole-node drain: {atf.get_node_events(node)!r}"
    assert "outright" not in (
        _events_in_state("Degraded") or ""
    ), f"States=Degraded matched the whole-node drain: {atf.get_node_events(node)!r}"


def test_two_reasons_share_one_event(dynamic_slurmds):
    """Two GPUs drained for different reasons share the node's one event.

    A node has one event open at a time, so the event covers everything
    drained on it and its reason names every distinct reason something was
    drained for. The GPUs are given UUIDs so the devices it names can be
    checked.

    The two drains are sent back to back, so they normally land in the same
    second. The event table is keyed on (node_name, time_start), so the second
    drain has to rewrite that row rather than be dropped by the collision, or
    the GPU it drained goes unaccounted.
    """
    _setup_uuid_gpus((0, "a100", GPU_A), (1, "a100", GPU_B))
    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:2")

    # No pause between them: the same-second collision is part of the point.
    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="why_a", fatal=True)
    atf.update_tres_drain(node, "gres/gpu", "Index=1", reason="why_b", fatal=True)

    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("(?:^|,)gres/gpu=2(?:,|$)", t)):
            break

    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected a single open event: {atf.get_node_events(node)!r}"
    assert not atf.get_overlapping_node_events(
        node
    ), f"events overlap each other: {atf.get_node_events(node)!r}"

    # Both reasons are recorded, joined in sorted order.
    assert (
        atf.get_open_node_event_reason(node) == "why_a,why_b"
    ), f"both reasons should be recorded: {atf.get_node_events(node)!r}"

    # The event names both devices, in sorted order.
    assert atf.get_open_node_event_tres_reference(node) == (
        f"gres/gpu:a100(IDX:0)(UUID:{GPU_A})," f"gres/gpu:a100(IDX:1)(UUID:{GPU_B})"
    ), f"the event should name both devices: {atf.get_node_events(node)!r}"


def test_resuming_one_reason_narrows_the_event(two_gpu_dynamic_node):
    """Resuming the TRES drained for one reason leaves the rest recorded, and
    resuming the rest closes the event.

    The node still has something drained, so its event stays open -- but it is
    reopened describing only what is left, both in the TRES counts and in the
    reason. Once nothing is drained the interval ends.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="goes", fatal=True)
    atf.update_tres_drain(node, "gres/gpu", "Index=1", reason="keep", fatal=True)

    for _ in atf.timer(fatal=True):
        r = atf.get_open_node_event_reason(node)
        if r == "goes,keep":
            break

    atf.update_tres_drain(node, "gres/gpu", "Index=0", action="Resume", fatal=True)

    for _ in atf.timer(fatal=True):
        r = atf.get_open_node_event_reason(node)
        if r == "keep":
            break

    tres = atf.get_open_node_event_tres(node)
    assert tres and re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", tres
    ), f"only the still-drained GPU should be covered: {atf.get_node_events(node)!r}"
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected a single open event: {atf.get_node_events(node)!r}"

    atf.update_tres_drain(node, "gres/gpu", "Index=ALL", action="Resume", fatal=True)

    for _ in atf.timer(fatal=True):
        count = atf.get_open_node_event_count(node)
        if count == 0:
            break


def test_cpu_and_gres_reasons_share_one_event(two_gpu_dynamic_node):
    """A drained CPU and a drained GPU are covered by the same event.

    They are different TRES drained for different reasons, but the node has
    one event, so it carries both counts and both reasons.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="gpu_why", fatal=True)
    atf.update_tres_drain(node, "cpu", "Index=0", reason="cpu_why", fatal=True)

    for _ in atf.timer(fatal=True):
        r = atf.get_open_node_event_reason(node)
        if r == "cpu_why,gpu_why":
            break

    tres = atf.get_open_node_event_tres(node)
    assert tres and re.search(
        r"(?:^|,)gres/gpu=1(?:,|$)", tres
    ), f"the drained GPU should be covered: {atf.get_node_events(node)!r}"
    assert tres and re.search(
        r"(?:^|,)cpu=1(?:,|$)", tres
    ), f"the drained CPU should be covered: {atf.get_node_events(node)!r}"
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected a single open event: {atf.get_node_events(node)!r}"


def test_reasons_differing_only_in_case_are_recorded_once(two_gpu_dynamic_node):
    """Reasons that differ only in case are one reason, not two.

    The reason is compared case insensitively, so recording both spellings
    would restamp the event every time the drain is refreshed.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="CaseTest", fatal=True)
    atf.update_tres_drain(node, "gres/gpu", "Index=1", reason="casetest", fatal=True)

    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("(?:^|,)gres/gpu=2(?:,|$)", t)):
            break

    reason = atf.get_open_node_event_reason(node)
    assert (
        reason and reason.lower() == "casetest"
    ), f"the reason should be recorded once: {atf.get_node_events(node)!r}"
    assert (
        atf.get_open_node_event_count(node) == 1
    ), f"expected a single open event: {atf.get_node_events(node)!r}"


def test_long_reasons_are_recorded_in_full(two_gpu_dynamic_node):
    """Reasons sharing a long prefix are told apart, however long they are.

    The reason column is not keyed and not truncated, so two reasons that
    agree for the first 42 characters are still two reasons.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    shared = "x" * 42
    atf.update_tres_drain(
        node, "gres/gpu", "Index=0", reason=f"{shared}_tail_one", fatal=True
    )
    atf.update_tres_drain(
        node, "gres/gpu", "Index=1", reason=f"{shared}_tail_two", fatal=True
    )

    for _ in atf.timer(fatal=True):
        t = atf.get_open_node_event_tres(node)
        if bool(t) and bool(re.search("(?:^|,)gres/gpu=2(?:,|$)", t)):
            break

    reason = atf.get_open_node_event_reason(node) or ""
    assert ("_tail_one" in reason) and (
        "_tail_two" in reason
    ), f"both reasons should be recorded in full: {atf.get_node_events(node)!r}"


def test_drain_event_survives_a_cluster_node_change(
    two_gpu_dynamic_node, dynamic_slurmds
):
    """Another node joining the cluster must not lose this node's drain event.

    A change to the cluster's node names makes slurmdbd close every open
    interval in the event table at once -- that statement carries no node name
    and no state, so it takes the drained TRES events of every node with it --
    and then ask the controller to send its node state again. Drained TRES
    events survive only because send_nodes_to_accounting() reports them for
    every node unconditionally.

    Were that re-send ever narrowed to the nodes it sends a whole-node event
    for, every node's drain would stop being accounted for the moment any node
    registered or was deleted, and nothing would say so.

    The interval itself is torn in two by the close and reopen, which is what
    already happens to whole-node events here; what must not happen is the
    drain going unrecorded.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="sweep_a", fatal=True)
    drained_at = int(time.time())
    for _ in atf.timer(fatal=True):
        found = _has_open_drain_event(node, "sweep_a")
        if found:
            break

    # The open event already passes _has_open_drain_event(), so after the sweep
    # look for its two halves: this row closed and a new one opened for it.
    opened = [
        row[6].strip()
        for row in _events_with_reason(node, "sweep_a")
        if row[4].strip() == "Unknown"
    ]
    assert len(opened) == 1, atf.get_node_events(node)

    # The event's start is keyed by the second. A re-send in the second of
    # the drain reopens this row instead of starting a new one.
    for _ in atf.timer(fatal=True):
        if int(time.time()) > drained_at:
            break

    # A second node registering changes the cluster's node names, which is what
    # triggers the close-everything sweep. Its own drain state is irrelevant.
    dynamic_slurmds(DYN_NODE_GOOD, f"Port={PORT_GOOD}")

    for _ in atf.timer(fatal=True):
        rows = _events_with_reason(node, "sweep_a")
        closed = [
            row
            for row in rows
            if row[6].strip() == opened[0] and row[4].strip() != "Unknown"
        ]
        reopened = [
            row
            for row in rows
            if row[6].strip() != opened[0]
            and row[4].strip() == "Unknown"
            and "gpu" in row[2]
        ]
        if closed and reopened:
            break
