############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
"""Validate the slurmd-supplied-field first-registration gate for
dynamic-normal nodes. The same gate
(node_ptr->boot_time > node_ptr->last_response in
validate_node_specs()) governs four fields delivered by slurmd:
topology (via dynamic_conf Topology=...), instance_id (--instance-id),
instance_type (--instance-type), and extra (--extra).

The gate fires on the first registration we see for the node
(last_response is 0, slurmd's reported boot_time is positive) and on
an actual node reboot (boot_time advances past last_response). It is
the same condition the existing reboot-detection branch uses to mark
a node "unexpectedly rebooted". Steady-state pings and slurmd
restarts without an actual reboot have boot_time < last_response and
are skipped, so admin overrides via scontrol update node ... are not
clobbered. last_response is state-saved, so the gate also survives
slurmctld restart: admin overrides persist across controller
downtime.

Topology scenarios (a/c/d/e/f/g/h) check two views in parallel: the
Topology= string on the node (scontrol show node), and the leaf
switch the node lives under (scontrol show topology). The first
reads node_ptr->topology_str directly, the second proves the
topology plugin's add/remove path also ran.

instance_id / instance_type / extra scenarios (i/j/k/l) cover the
sibling field-apply paths in the same gated block; each field has
its own if-statement inside the gate, so per-field coverage guards
against one being broken without breaking the others.
"""
import re

import pytest

import atf

PORT_BASE = 61500


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("creates a custom topology.yaml and restarts slurmctld")
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="being able to change dynamic topology after reboot added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="being able to change dynamic topology after reboot added in 26.11",
    )
    # One static node to bootstrap; one dynamic-normal node per test.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter("MaxNodeCount", 2)

    # Scenario h depends on the "Node unexpectedly rebooted" branch in
    # validate_node_specs(), which is skipped when ReturnToService=2
    # (RETURN_TO_SERVICE_ALL). Force a value that lets the branch fire.
    atf.require_config_parameter("ReturnToService", "1")

    # A tree topology with three named leaf switches, so the dynamic
    # node's --conf "Topology=tree_topo:sw_*" places it under that
    # switch and we can verify placement via scontrol show topology.
    # node_ptr->topology_str is interpreted by the multi-topology
    # dispatcher as "<topology_name>:<unit>" (see
    # src/interfaces/topology.c:topology_g_add_rm_node), so the
    # tree_topo: prefix is required.
    atf.require_config_file(
        "topology.yaml",
        """
- topology: tree_topo
  cluster_default: true
  tree:
    switches:
      - switch: sw_root
        children: sw_alpha,sw_plain,sw_gamma
      - switch: sw_alpha
      - switch: sw_plain
      - switch: sw_gamma
""",
    )

    atf.require_slurm_running()


def _slurmd_cmd(name, port, conf_extra="", slurmd_args=""):
    conf = f"Port={port}"
    if conf_extra:
        conf = f"{conf} {conf_extra}"
    cmd = f"{atf.properties['slurm-sbin-dir']}/slurmd -N {name} -Z " f"--conf '{conf}'"
    if slurmd_args:
        cmd = f"{cmd} {slurmd_args}"
    return cmd


def _start_dynamic_slurmd(name, port, conf_extra="", slurmd_args=""):
    """Start a dynamic-normal slurmd and wait until it is IDLE."""
    atf.run_command(
        _slurmd_cmd(name, port, conf_extra, slurmd_args),
        user="root",
        fatal=True,
    )
    atf.repeat_until(
        lambda: name in atf.get_nodes(quiet=True),
        lambda found: found,
        timeout=30,
        fatal=True,
    )
    assert atf.wait_for_node_state(
        name, "IDLE", timeout=30, fatal=True
    ), f"Dynamic node {name} should reach IDLE state"


def _kill_dynamic_slurmd(name):
    """Kill the slurmd for the given dynamic node and wait for the process
    to exit so we can safely start it again."""
    pid = atf.run_command_output(
        f"pgrep -f '{atf.properties['slurm-sbin-dir']}/slurmd -N {name} -Z'",
        fatal=False,
    ).strip()
    if not pid:
        return
    atf.run_command(f"kill {pid}", user="root", fatal=False)
    atf.repeat_until(
        lambda: atf.run_command_output(
            f"pgrep -f '{atf.properties['slurm-sbin-dir']}/slurmd -N {name} -Z'",
            fatal=False,
        ).strip(),
        lambda out: not out,
        timeout=15,
        fatal=False,
    )


def _delete_dynamic_node(name):
    _kill_dynamic_slurmd(name)
    atf.run_command(f"scontrol delete NodeName={name}", user="slurm", fatal=False)


def _wait_for_slurmd_reregister(name, old_start_time, timeout=30):
    """Wait until node_ptr->slurmd_start_time differs from old_start_time,
    indicating a freshly started slurmd has registered with the
    controller. wait_for_node_state(IDLE) alone is unreliable when the
    node was already IDLE before the slurmd kill -- the state may not
    have transitioned, so the assertion can read stale field values
    before the new registration arrives."""
    ok = atf.repeat_until(
        lambda: atf.get_node_parameter(name, "slurmd_start_time"),
        lambda t: t and t != old_start_time,
        timeout=timeout,
        fatal=False,
    )
    if not ok:
        pytest.fail(
            f"Slurmd did not re-register on {name} within {timeout}s "
            f"(slurmd_start_time still {old_start_time})"
        )


def _node_topology(name):
    """Return the node's currently reported Topology= string, or None."""
    return atf.get_node_parameter(name, "topology")


def _assert_topology(name, expected, timeout=10):
    """Poll until the node's topology matches `expected`, or fail with a
    diagnostic showing the value that was actually observed.

    Pass expected=None to assert the topology is cleared (None or empty)."""
    if expected is None:
        ok = atf.repeat_until(
            lambda: _node_topology(name),
            lambda topo: not topo,
            timeout=timeout,
            fatal=False,
        )
    else:
        ok = atf.repeat_until(
            lambda: _node_topology(name),
            lambda topo: topo == expected,
            timeout=timeout,
            fatal=False,
        )
    if not ok:
        pytest.fail(
            f"Expected topology {expected!r} on {name}, got "
            f"{_node_topology(name)!r}"
        )


def _switch_for_node(name):
    """Return the leaf SwitchName containing `name`, or None.

    Parses 'scontrol show topology' output. Only level-0 (leaf) switches
    list nodes, so we ignore intermediate switches."""
    out = atf.run_command_output("scontrol show topology", fatal=False)
    for line in out.splitlines():
        m = re.match(r"SwitchName=(\S+) Level=0 .*Nodes=(\S+)", line)
        if not m:
            continue
        sw, nodes_expr = m.group(1), m.group(2)
        if name in atf.node_range_to_list(nodes_expr):
            return sw
    return None


def _assert_node_under_switch(name, expected, timeout=10):
    """Poll until `name` lives under SwitchName=expected.

    Pass expected=None to assert the node is under no configured
    leaf switch (i.e. its topology was cleared)."""
    ok = atf.repeat_until(
        lambda: _switch_for_node(name),
        lambda sw: sw == expected,
        timeout=timeout,
        fatal=False,
    )
    if not ok:
        topo_dump = atf.run_command_output("scontrol show topology", fatal=False)
        pytest.fail(
            f"Expected {name} under switch {expected!r}, got "
            f"{_switch_for_node(name)!r}. scontrol show topology:\n"
            f"{topo_dump}"
        )


def test_baseline_topology_from_slurmd_conf():
    """Scenario a: dynamic-normal slurmd with Topology=tree_topo:sw_alpha in its
    --conf registers under switch sw_alpha."""
    name = "node10"
    try:
        _start_dynamic_slurmd(name, PORT_BASE, "Topology=tree_topo:sw_alpha")
        _assert_topology(name, "tree_topo:sw_alpha")
        _assert_node_under_switch(name, "sw_alpha")
    finally:
        _delete_dynamic_node(name)


def test_admin_override_survives_slurmd_ping():
    """Scenario c: after scontrol update node Topology=, a slurmd ping
    that still reports the original Topology must not clobber the
    admin override.

    Re-registration is forced by killing and restarting slurmd with
    the same --conf; from the controller's perspective this is a
    registration RPC carrying the original Topology, which is exactly
    the ping case for the new validate_node_specs() gate."""
    name = "node11"
    port = PORT_BASE + 1
    try:
        _start_dynamic_slurmd(name, port, "Topology=tree_topo:sw_alpha")
        _assert_topology(name, "tree_topo:sw_alpha")
        _assert_node_under_switch(name, "sw_alpha")

        atf.run_command(
            f"scontrol update NodeName={name} Topology=tree_topo:sw_plain",
            user="slurm",
            fatal=True,
        )
        _assert_topology(name, "tree_topo:sw_plain")
        _assert_node_under_switch(name, "sw_plain")

        # Re-register with the same --conf (slurmd still says sw_alpha).
        old_start = atf.get_node_parameter(name, "slurmd_start_time")
        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, "Topology=tree_topo:sw_alpha"),
            user="root",
            fatal=True,
        )
        _wait_for_slurmd_reregister(name, old_start)
        _assert_topology(name, "tree_topo:sw_plain")
        _assert_node_under_switch(name, "sw_plain")
    finally:
        _delete_dynamic_node(name)


def test_admin_override_survives_slurmd_restart():
    """Scenario d: explicit slurmd kill/restart with the same --conf
    after an admin override -- override survives.

    Distinct from scenario c only in the framing (a full daemon
    restart vs. a registration RPC); both hit the same controller
    code path, so this test mainly guards against future drift if
    that ever changes."""
    name = "node12"
    port = PORT_BASE + 2
    try:
        _start_dynamic_slurmd(name, port, "Topology=tree_topo:sw_alpha")
        atf.run_command(
            f"scontrol update NodeName={name} Topology=tree_topo:sw_plain",
            user="slurm",
            fatal=True,
        )
        _assert_topology(name, "tree_topo:sw_plain")
        _assert_node_under_switch(name, "sw_plain")

        old_start = atf.get_node_parameter(name, "slurmd_start_time")
        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, "Topology=tree_topo:sw_alpha"),
            user="root",
            fatal=True,
        )
        _wait_for_slurmd_reregister(name, old_start)
        _assert_topology(name, "tree_topo:sw_plain")
        _assert_node_under_switch(name, "sw_plain")
    finally:
        _delete_dynamic_node(name)


def test_slurmd_new_topology_ignored():
    """Scenario e: when slurmd restarts with a different Topology=, the
    new value is IGNORED -- slurmd does not take topology ownership
    back. To re-apply a new Topology= the operator must scontrol delete
    the node and let it re-register."""
    name = "node13"
    port = PORT_BASE + 3
    try:
        _start_dynamic_slurmd(name, port, "Topology=tree_topo:sw_alpha")
        _assert_topology(name, "tree_topo:sw_alpha")
        _assert_node_under_switch(name, "sw_alpha")

        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, "Topology=tree_topo:sw_gamma"),
            user="root",
            fatal=True,
        )
        assert atf.wait_for_node_state(name, "IDLE", timeout=30, fatal=True)
        # Slurmd's new Topology= must be ignored: the node stays under
        # the topology assigned on the first registration.
        _assert_topology(name, "tree_topo:sw_alpha")
        _assert_node_under_switch(name, "sw_alpha")
    finally:
        _delete_dynamic_node(name)


def test_topology_retained_when_slurmd_drops_topology():
    """Scenario f: when slurmd restarts without Topology= in --conf,
    the topology assigned on the first registration is retained
    (slurmd's drop is not a take-back signal)."""
    name = "node14"
    port = PORT_BASE + 4
    try:
        _start_dynamic_slurmd(name, port, "Topology=tree_topo:sw_alpha")
        _assert_topology(name, "tree_topo:sw_alpha")
        _assert_node_under_switch(name, "sw_alpha")

        _kill_dynamic_slurmd(name)
        # Restart slurmd without Topology= -- the prior value must stay.
        atf.run_command(_slurmd_cmd(name, port), user="root", fatal=True)
        assert atf.wait_for_node_state(name, "IDLE", timeout=30, fatal=True)
        _assert_topology(name, "tree_topo:sw_alpha")
        _assert_node_under_switch(name, "sw_alpha")
    finally:
        _delete_dynamic_node(name)


def test_admin_override_survives_slurmctld_restart():
    """Scenario g: last_response is persisted in the node state file,
    so an admin override placed after slurmd's first registration must
    survive a slurmctld restart and the next slurmd re-registration:
    on restore the gate (boot_time > last_response) correctly skips
    when slurmd reports the same OS boot it had before the controller
    went down."""
    name = "node15"
    port = PORT_BASE + 5
    try:
        _start_dynamic_slurmd(name, port, "Topology=tree_topo:sw_alpha")
        atf.run_command(
            f"scontrol update NodeName={name} Topology=tree_topo:sw_plain",
            user="slurm",
            fatal=True,
        )
        _assert_topology(name, "tree_topo:sw_plain")
        _assert_node_under_switch(name, "sw_plain")

        # Restart slurmctld -- boot_time must be reloaded from the
        # state file so the next slurmd registration is recognized as
        # "not the first" and leaves topology_str alone.
        atf.restart_slurmctld(clean=False)

        # Force a slurmd re-registration carrying the same --conf.
        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, "Topology=tree_topo:sw_alpha"),
            user="root",
            fatal=True,
        )
        assert atf.wait_for_node_state(name, "IDLE", timeout=30, fatal=True)
        _assert_topology(name, "tree_topo:sw_plain")
        _assert_node_under_switch(name, "sw_plain")
    finally:
        _delete_dynamic_node(name)


def test_reboot_during_slurmctld_downtime_detected():
    """Scenario h: a node reboot during slurmctld downtime must be
    detected on the next registration after the controller comes
    back up. State-saved boot_time makes the "Node unexpectedly
    rebooted" branch in validate_node_specs fire correctly across
    controller restart, where previously boot_time reset to 0 on
    every controller start and the check could not fire on the
    first post-restart registration.

    Simulated by killing slurmd while slurmctld is down and
    restarting it with -b, which sets slurmd's conf->boot_time to
    "now" (slurmd.c) so the controller derives a boot_time later
    than the saved last_response."""
    name = "node16"
    port = PORT_BASE + 6
    try:
        _start_dynamic_slurmd(name, port, "Topology=tree_topo:sw_alpha")
        assert atf.wait_for_node_state(name, "IDLE", timeout=30, fatal=True)

        # Stop slurmctld; slurmd keeps running but its pings fail.
        atf.stop_slurmctld()

        # Kill slurmd and restart with -b. The -b flag sets slurmd's
        # conf->boot_time = time(NULL), so on next registration the
        # controller-side boot_time = now - up_time will be slurmd's
        # restart time, which is later than the last_response we have
        # in the state file from the pre-shutdown registration.
        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, "Topology=tree_topo:sw_alpha") + " -b",
            user="root",
            fatal=True,
        )

        # Bring slurmctld back; state is restored (including boot_time
        # and last_response). Slurmd's pending registration is then
        # processed and the reboot-detection branch should fire.
        atf.start_slurmctld(clean=False)

        assert atf.wait_for_node_state(
            name, "DOWN", timeout=30, fatal=True
        ), "Node should be marked DOWN after reboot-during-downtime"

        reason = atf.get_node_parameter(name, "reason")
        assert (
            reason and "rebooted" in reason.lower()
        ), f"Expected reboot-related reason on {name}, got: {reason!r}"
    finally:
        _delete_dynamic_node(name)


# ----------------------------------------------------------------------------
# instance_id / instance_type / extra share the same first-registration gate
# (boot_time > last_response) as topology. The scenarios below exercise the
# parallel field-apply paths in the unified gated block in
# validate_node_specs(): each field is its own if-statement inside the gate,
# so per-field coverage guards against one being broken without breaking the
# others.
# ----------------------------------------------------------------------------


def _assert_node_field(name, field, expected, timeout=10):
    """Poll until atf.get_node_parameter(name, field) matches expected."""
    ok = atf.repeat_until(
        lambda: atf.get_node_parameter(name, field),
        lambda v: v == expected,
        timeout=timeout,
        fatal=False,
    )
    if not ok:
        pytest.fail(
            f"Expected {field}={expected!r} on {name}, got "
            f"{atf.get_node_parameter(name, field)!r}"
        )


def test_baseline_instance_id_type_extra_from_slurmd_conf():
    """Scenario i: dynamic-normal slurmd with --instance-id, --instance-type,
    and --extra on the command line populates the three corresponding node
    fields on first registration."""
    name = "node17"
    port = PORT_BASE + 7
    try:
        _start_dynamic_slurmd(
            name,
            port,
            slurmd_args="--instance-id=id-i --instance-type=type-i --extra=extra-i",
        )
        _assert_node_field(name, "instance_id", "id-i")
        _assert_node_field(name, "instance_type", "type-i")
        _assert_node_field(name, "extra", "extra-i")
    finally:
        _delete_dynamic_node(name)


@pytest.mark.parametrize(
    "name, field,slurmd_flag,scontrol_kw,orig,admin",
    [
        ("node18", "instance_id", "--instance-id", "InstanceId", "id-orig", "id-admin"),
        (
            "node19",
            "instance_type",
            "--instance-type",
            "InstanceType",
            "type-orig",
            "type-admin",
        ),
        ("node20", "extra", "--extra", "Extra", "extra-orig", "extra-admin"),
    ],
)
def test_admin_override_survives_slurmd_restart_field(
    name, field, slurmd_flag, scontrol_kw, orig, admin
):
    """Scenarios j/k/l: after a scontrol update of the field, a slurmd
    kill/restart that re-reports the original value does not clobber
    the admin override. One pytest node per field so a per-field
    regression localizes to one apply block in the unified gate."""
    port = PORT_BASE + 8 + ["instance_id", "instance_type", "extra"].index(field)
    try:
        _start_dynamic_slurmd(name, port, slurmd_args=f"{slurmd_flag}={orig}")
        _assert_node_field(name, field, orig)

        atf.run_command(
            f"scontrol update NodeName={name} {scontrol_kw}={admin}",
            user="slurm",
            fatal=True,
        )
        _assert_node_field(name, field, admin)

        old_start = atf.get_node_parameter(name, "slurmd_start_time")
        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, slurmd_args=f"{slurmd_flag}={orig}"),
            user="root",
            fatal=True,
        )
        _wait_for_slurmd_reregister(name, old_start)
        _assert_node_field(name, field, admin)
    finally:
        _delete_dynamic_node(name)


def test_slurmd_reboot_applies_new_topology():
    """Scenario m: when slurmd is restarted with -b (simulating a node
    reboot) and a different Topology= in --conf, the new topology must
    be applied -- including over an admin override placed via
    scontrol after first registration. This exercises the reboot
    half of the first-reg/reboot gate: boot_time advances past
    last_response and the field-apply block in validate_node_specs
    re-runs for any node type. The reboot-detection branch also
    drains the node DOWN with "unexpectedly rebooted", but the
    field-apply block runs first in the same call, so topology_str
    is updated to slurmd's reboot value (overriding the admin's
    pre-reboot setting).

    This scenario was added because without the topology-on-reboot
    change, the apply block is gated on
    IS_NODE_CLOUD && (was_powering_up || was_powered_down) and never
    fires for dynamic-normal nodes -- subsequent slurmd reboots
    silently keep the admin's pre-reboot topology."""
    name = "node21"
    port = PORT_BASE + 11
    try:
        _start_dynamic_slurmd(name, port, "Topology=tree_topo:sw_alpha")
        _assert_topology(name, "tree_topo:sw_alpha")
        _assert_node_under_switch(name, "sw_alpha")

        # Admin override before the reboot. On a normal (no-reboot)
        # slurmd restart this would survive (see scenarios c/d), but
        # a real reboot brings in slurmd's new view.
        atf.run_command(
            f"scontrol update NodeName={name} Topology=tree_topo:sw_plain",
            user="slurm",
            fatal=True,
        )
        _assert_topology(name, "tree_topo:sw_plain")
        _assert_node_under_switch(name, "sw_plain")

        # Kill slurmd and restart with -b AND a different Topology=.
        # The -b sets conf->boot_time = now so the controller-derived
        # boot_time on the next registration is later than the saved
        # last_response, firing the gate.
        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, "Topology=tree_topo:sw_gamma") + " -b",
            user="root",
            fatal=True,
        )
        # The node is marked DOWN by the reboot-detection branch, but
        # topology_str is updated by the field-apply block earlier in
        # the same validate_node_specs() call -- replacing the admin
        # override with slurmd's reboot value.
        assert atf.wait_for_node_state(
            name, "DOWN", timeout=30, fatal=True
        ), "Node should be marked DOWN after simulated reboot"
        _assert_topology(name, "tree_topo:sw_gamma")
        _assert_node_under_switch(name, "sw_gamma")
    finally:
        _delete_dynamic_node(name)


@pytest.mark.parametrize(
    "name,field,slurmd_flag,scontrol_kw,orig,admin,new",
    [
        (
            "node22",
            "instance_id",
            "--instance-id",
            "InstanceId",
            "id-orig",
            "id-admin",
            "id-new",
        ),
        (
            "node23",
            "instance_type",
            "--instance-type",
            "InstanceType",
            "type-orig",
            "type-admin",
            "type-new",
        ),
        (
            "node24",
            "extra",
            "--extra",
            "Extra",
            "extra-orig",
            "extra-admin",
            "extra-new",
        ),
    ],
)
def test_slurmd_reboot_applies_new_field(
    name, field, slurmd_flag, scontrol_kw, orig, admin, new
):
    """Scenarios n/o/p: when slurmd is restarted with -b (simulating a
    node reboot) and a different value for the field, the new value
    is applied -- including over an admin override placed between
    the original registration and the reboot. Mirrors scenario m for
    the corresponding field-apply path in the unified gated block.
    One pytest node per field so a per-field regression localizes."""
    port = PORT_BASE + 12 + ["instance_id", "instance_type", "extra"].index(field)
    try:
        _start_dynamic_slurmd(name, port, slurmd_args=f"{slurmd_flag}={orig}")
        _assert_node_field(name, field, orig)

        atf.run_command(
            f"scontrol update NodeName={name} {scontrol_kw}={admin}",
            user="slurm",
            fatal=True,
        )
        _assert_node_field(name, field, admin)

        _kill_dynamic_slurmd(name)
        atf.run_command(
            _slurmd_cmd(name, port, slurmd_args=f"{slurmd_flag}={new} -b"),
            user="root",
            fatal=True,
        )
        assert atf.wait_for_node_state(
            name, "DOWN", timeout=30, fatal=True
        ), "Node should be marked DOWN after simulated reboot"
        _assert_node_field(name, field, new)
    finally:
        _delete_dynamic_node(name)
