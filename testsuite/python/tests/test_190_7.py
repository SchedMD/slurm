############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Degraded and drained GRES coverage for dynamic-normal slurmds (`slurmd -Z`).

Three sections, each under its own banner below: single-node drain/resume and
MaxDrainedTres, the same drain seen by a job spanning a discontinuous node pair,
and selecting a device by UUID -- whichever way the device got that UUID, from
AutoDetect or from a gres.conf UUID option.

What is deliberately elsewhere: the shared request validation, which is not
specific to GPUs (test_190_2), and an absent GPU keeping its saved index,
which needs a static node (test_190_14).

A few tests here pair a GPU with a count-only bandwidth GRES, which is why the
module requires both GRES types. That is not stray count-only coverage -- what
they check is behaviour *between* two GRES, which one GRES cannot express: that
MaxDrainedTres holds a separate limit per GRES, and that a bitmap drain and a
count drain both survive one restart or re-registration. Count-only GRES on its
own is test_190_6, whose node has no GPU to pair with.

The nic GRES type is there for the one test that drains a non-GPU file GRES
by the UUID its gres.conf line sets.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Node names need to be in the form of nodeNN for the s2n variant. The nodes
# are deliberately discontinuous: DYN_NODE_SKIP is registered but never
# targeted (it absorbs the scheduler's first-choice bias) and DYN_NODE_GOOD
# sits between the two drain nodes, so a job pinned to DYN_NODE/DYN_NODE_B
# exercises the discontinuous-bitmap paths.
DYN_NODE_SKIP = "node99"  # index 0: registered but not used in jobs
DYN_NODE = "node100"  # index 1: primary drain node (NODE_A)
DYN_NODE_GOOD = "node101"  # index 2: healthy node between DYN_NODE and _B
DYN_NODE_B = "node102"  # index 3: secondary drain node (NODE_B)

# Each dynamic slurmd on the same host must listen on a distinct port. Without
# explicit Port=, all four slurmds default to SlurmdPort (e.g. 6818) and only
# the first one can bind; subsequent nodes fail to register.
PORT_SKIP = 61599
PORT_A = 61600
PORT_GOOD = 61601
PORT_B = 61602

# Fake GPU device UUIDs, in fake_gpus.conf device order.
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"
GPU_C = "GPU-cccccccc-2222-2222-2222-222222222222"

# UUIDs for a non-GPU file GRES, set by the gres.conf UUID option.
NIC_A = "NIC-aaaaaaaa-0000-0000-0000-000000000000"
NIC_B = "NIC-bbbbbbbb-1111-1111-1111-111111111111"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: GRES drain added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots and MaxDrainedTres added in 26.11",
    )
    atf.require_config_parameter("MaxNodeCount", 16)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "bandwidth")
    atf.require_config_parameter_includes("GresTypes", "nic")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_tty(3)
    atf.require_slurm_running()
    atf.require_config_file("gres.conf", "Autodetect=full\n")


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


def _reconfig_node_max_drained_tres(node, limit):
    """Append a NodeName line and reconfigure to apply a new MaxDrainedTres."""
    conf_path = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    atf.run_command(
        f"printf '\\n%s\\n' 'NodeName={node} MaxDrainedTres={limit}' >> {conf_path}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    atf.run_command(
        "scontrol reconfigure",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(autouse=True)
def default_gres_conf():
    """Leave gres.conf as dynamic nodes default to for the next test."""
    yield
    atf.require_config_file("gres.conf", "Autodetect=full\n")


@pytest.fixture
def slurm_conf_restored():
    """Restore slurm.conf after a test that edits it directly.

    A test that appends to slurm.conf (see _reconfig_node_max_drained_tres)
    must undo it: restart_slurm() reuses the existing file, so a leftover
    ``NodeName={DYN_NODE} ...`` line would turn the dynamic node into a
    statically-configured one for later tests, making them ignore its
    ``--conf`` GRES. Tests that call _reconfig_node_max_drained_tres() request
    this fixture so the edit is reverted on teardown (pass or fail).
    """
    conf = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    backup = f"{conf}.190_7.bak"
    atf.run_command(
        f"cp {conf} {backup}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    yield
    atf.run_command(
        f"mv {backup} {conf}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


# ---------------------------------------------------------------------------
# Single-node GPU drain, resume and MaxDrainedTres.
#
# What a drain does to one node: an already-allocated device keeps running while
# new allocations skip it, a resume puts it back, MaxDrainedTres escalates to
# draining the whole node once too much of it is gone, and all of it survives a
# controller restart and a slurmd re-registration.
# ---------------------------------------------------------------------------


def test_gres_drain_allows_allocated_gpu(dynamic_slurmds):
    """An in-use drained GPU and its job survive controller recovery."""
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf((0, "(null)")))
    atf.restart_slurm(clean=True, quiet=True)

    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:1")
    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=gpu:1 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="test")
    assert result["exit_code"] == 0, "command should succeed"

    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert re.search(
        r"DrainedTRES=.*gpu\(IDX:0\)\(test\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"

    # Recovery replays existing GRES allocations with new_alloc=false.
    # The drained device is already part of this job, so it must not be
    # subtracted a second time from the capacity used to restore the job:
    # the replay would fail and the node would come back believing the
    # GPU is free while the job still holds it.
    atf.restart_slurmctld(clean=False, quiet=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
    for _ in atf.timer(fatal=True):
        detail = atf.get_node_detail(node)
        if re.search("GresUsed=gpu:(?:[^:]*:)?1\\(IDX:0\\)", detail) is not None:
            break
    assert re.search(
        r"DrainedTRES=.*gpu\(IDX:0\)\(test\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"


@pytest.mark.parametrize("selector", ["Index=0", f"Uuid={GPU_A}"])
def test_gres_drain_excludes_gpu_from_new_allocations(two_gpu_dynamic_node, selector):
    """A drained GPU is not selected for a later job, named by index or by UUID."""
    node = two_gpu_dynamic_node(uuids=(GPU_A, GPU_B))
    result = atf.update_tres_drain(node, "gres/gpu", selector, reason="test")
    assert result["exit_code"] == 0, "command should succeed"

    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=gpu:1 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
    job = atf.get_job(job_id)
    # GPU_A is index 0 and is the drained device either way, so the job
    # must land on the other GPU (index 1).
    assert re.search(r"gpu(?::a100)?:1\(IDX:1\)", str(job)), job


def test_gres_drain_by_gres_type(dynamic_slurmds):
    """Draining by GRES type targets only devices of that type."""
    atf.require_config_file(
        "fake_gpus.conf",
        atf.fake_gpus_conf((0, "a100", "(null)"), (1, "v100", "(null)")),
    )
    atf.restart_slurm(clean=True, quiet=True)

    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:1,gpu:v100:1")

    # Index 1 is the v100, so requesting it as an a100 is rejected.
    result = atf.update_tres_drain(node, "gres/gpu:a100", "Index=1", reason="wrong")
    assert result["exit_code"] != 0, result
    assert "Invalid GRES specification" in result["stderr"], result

    # Draining a GRES type drains only that type's device (the v100 at index 1).
    result = atf.update_tres_drain(
        node, "gres/gpu:v100", "Index=ALL", reason="bad_v100"
    )
    assert result["exit_code"] == 0, "command should succeed"
    detail = atf.get_node_detail(node)
    assert re.search(r"DrainedTRES=.*gpu\(IDX:1\)\(bad_v100\)", detail), detail
    assert "IDX:0" not in detail, detail


def test_max_drained_tres_admin_drain_over_limit_drains_node(two_gpu_dynamic_node):
    """Admin draining more GRES than MaxDrainedTres drains the node itself."""
    node = two_gpu_dynamic_node("Gres=gpu:a100:2 MaxDrainedTres=gres/gpu:1")

    # Draining one GPU is within the limit: node degraded but not drained.
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="one")
    assert result["exit_code"] == 0, "command should succeed"
    state = atf.get_node_parameter(node, "state") or ""
    assert "DEGRADED" in state, f"expected DEGRADED node, got State={state!r}"
    assert "DRAIN" not in state, f"node drained too early, State={state!r}"

    # Draining a second GPU exceeds the limit: the node is drained.
    result = atf.update_tres_drain(node, "gres/gpu", "Index=1", reason="two")
    assert result["exit_code"] == 0, "command should succeed"
    assert atf.wait_for_node_state(
        node, "DRAIN"
    ), "node was not drained after exceeding MaxDrainedTres"
    assert (
        atf.get_node_parameter(node, "reason")
        == "gpu has 2 drained, exceeding MaxDrainedTres limit of 1"
    ), atf.get_node_parameter(node, "reason")


def test_max_drained_tres_redrain_with_jobs_names_the_limit(two_gpu_dynamic_node):
    """RESUME_KEEP_TRES of a node draining with jobs redrains it for the limit.

    The admin's reason went with the drain it named, so the node is drained
    again with the MaxDrainedTres reason, not the old one.
    """
    node = two_gpu_dynamic_node("Gres=gpu:a100:2 MaxDrainedTres=gres/gpu:1")
    job_id = atf.submit_job_sbatch(f"-w {node} --wrap 'sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(job_id, "RUNNING"), atf.get_job(job_id)

    atf.run_command(
        f"scontrol update NodeName={node} State=DRAIN Reason=maint",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.update_tres_drain(node, "gres/gpu", "Index=0-1", reason="bad", fatal=True)
    atf.run_command(
        f"scontrol update NodeName={node} State=RESUME_KEEP_TRES",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert atf.wait_for_node_state(node, "DRAIN"), atf.get_node_detail(node)
    assert (
        atf.get_node_parameter(node, "reason")
        == "gpu has 2 drained, exceeding MaxDrainedTres limit of 1"
    ), atf.get_node_parameter(node, "reason")


def test_max_drained_tres_reconfig_tightens_limit(
    slurm_conf_restored, two_gpu_dynamic_node
):
    """Tightening MaxDrainedTres on reconfigure drains an over-limit node."""
    node = two_gpu_dynamic_node("Gres=gpu:a100:2 MaxDrainedTres=gres/gpu:2")
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="one")
    assert result["exit_code"] == 0, result
    state = atf.get_node_parameter(node, "state") or ""
    assert "DEGRADED" in state, f"expected DEGRADED node, got State={state!r}"
    assert "DRAIN" not in state, f"node drained too early, State={state!r}"

    _reconfig_node_max_drained_tres(node, "gres/gpu:0")

    # The controller must enforce the new limit during reconfigure itself.
    # Waiting here could let a later slurmd registration mask a missing
    # reconfigure-time evaluation.
    state = atf.get_node_parameter(node, "state") or ""
    assert "DRAIN" in state, (
        "node was not drained after MaxDrainedTres tightened on reconfigure: "
        f"{state!r}"
    )
    assert (
        atf.get_node_parameter(node, "reason")
        == "gpu has 1 drained, exceeding MaxDrainedTres limit of 0"
    ), atf.get_node_parameter(node, "reason")


@pytest.mark.parametrize(
    "selector, drained_idx", [("Index=0", 0), (f"Uuid={GPU_B}", 1)]
)
def test_gres_resume_restores_scheduling(two_gpu_dynamic_node, selector, drained_idx):
    """A resume clears the drain, leaves DEGRADED, and reschedules the GPU.

    The drained GPU is named by index or by UUID; either way the resume must
    fully undo the drain.
    """
    node = two_gpu_dynamic_node(uuids=(GPU_A, GPU_B))

    result = atf.update_tres_drain(node, "gres/gpu", selector, reason="test")
    assert result["exit_code"] == 0, result
    assert "DEGRADED" in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should be DEGRADED"
    assert re.search(
        rf"DrainedTRES=.*gpu\(IDX:{drained_idx}\)\(test\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"

    result = atf.update_tres_drain(node, "gres/gpu", selector, action="Resume")
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        node, "DEGRADED", reverse=True
    ), f"node still DEGRADED after resume: {atf.get_node_parameter(node, 'state')!r}"
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)

    # Both GPUs must be schedulable again (gpu:2 would stay pending otherwise).
    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=gpu:2 --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
    job = atf.get_job(job_id)
    assert "IDX:0" in str(job), job


def test_multi_type_max_drained_tres(slurm_conf_restored, dynamic_slurmds):
    """MaxDrainedTres names every TRES that is over its own limit.

    The node is not drained until both pools are already past the limits that
    reconfigure will install. A breach on a node that is already DRAIN does not
    rewrite the reason, so the two parts have to be recorded together.
    """
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        "Name=gpu File=/dev/tty0 Type=a100\n"
        "Name=gpu File=/dev/tty1 Type=a100\n"
        "Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n",
    )
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, "(null)"), (1, "(null)"))
    )
    atf.restart_slurm(clean=True, quiet=True)

    node = dynamic_slurmds(
        DYN_NODE,
        extra_conf="Gres=gpu:a100:2,bandwidth:lustre:4",
    )

    result = atf.update_tres_drain(node, "gres/bandwidth", "Count=3", reason="bw")
    assert result["exit_code"] == 0, result
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0,1", reason="gpu")
    assert result["exit_code"] == 0, result
    state = atf.get_node_parameter(node, "state") or ""
    assert "DEGRADED" in state, state
    assert "DRAIN" not in state, state

    _reconfig_node_max_drained_tres(node, "gres/gpu:1,gres/bandwidth:2")

    state = atf.get_node_parameter(node, "state") or ""
    assert "DRAIN" in state, (
        "node not drained after both GRES exceeded their limits: " f"state={state!r}"
    )
    gpu = "gpu has 2 drained, exceeding MaxDrainedTres limit of 1"
    bandwidth = "bandwidth has 3 drained, exceeding MaxDrainedTres limit of 2"
    reason = atf.get_node_parameter(node, "reason")
    assert reason in (f"{gpu}, {bandwidth}", f"{bandwidth}, {gpu}"), reason


def test_aggregate_and_typed_max_drained_tres(slurm_conf_restored, dynamic_slurmds):
    """An aggregate GRES limit and a type limit on that GRES both apply.

    gres/gpu:2 allows two drained GPUs. gres/gpu:a100:1 allows one drained
    a100. Two a100s cross only the type limit. One a100 and two l40s cross
    only the aggregate.
    """
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        "Name=gpu File=/dev/tty0 Type=a100\n"
        "Name=gpu File=/dev/tty1 Type=a100\n"
        "Name=gpu File=/dev/tty2 Type=l40s\n"
        "Name=gpu File=/dev/tty3 Type=l40s\n",
    )
    atf.require_config_file(
        "fake_gpus.conf",
        atf.fake_gpus_conf(
            (0, "a100", GPU_A),
            (1, "a100", GPU_B),
            (2, "l40s", GPU_C),
            (3, "l40s", "GPU-dddddddd-3333-3333-3333-333333333333"),
        ),
    )
    atf.restart_slurm(clean=True, quiet=True)
    node = dynamic_slurmds(
        DYN_NODE,
        extra_conf=(
            "Gres=gpu:a100:2,gpu:l40s:2 " "MaxDrainedTres=gres/gpu:2,gres/gpu:a100:1"
        ),
    )

    result = atf.update_tres_drain(node, "gres/gpu:a100", f"Uuid={GPU_A}", reason="agg")
    assert result["exit_code"] == 0, result
    result = atf.update_tres_drain(node, "gres/gpu:l40s", "Index=ALL", reason="agg")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(node, "DRAIN"), atf.get_node_parameter(node, "state")
    reason = atf.get_node_parameter(node, "reason")
    assert reason == "gpu has 3 drained, exceeding MaxDrainedTres limit of 2", reason

    atf.run_command(
        f"scontrol update NodeName={node} State=RESUME",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        detail = atf.get_node_detail(node) or ""
        if "DRAIN" not in detail and "a100" in detail and "l40s" in detail:
            break

    result = atf.update_tres_drain(node, "gres/gpu:a100", "Index=ALL", reason="type")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(node, "DRAIN"), atf.get_node_parameter(node, "state")
    reason = atf.get_node_parameter(node, "reason")
    assert (
        reason == "gpu:a100 has 2 drained, exceeding MaxDrainedTres limit of 1"
    ), reason


@pytest.mark.parametrize(
    "selector, drained_idx", [("Index=0", 0), (f"Uuid={GPU_B}", 1)]
)
def test_drain_persists_across_slurmctld_restart(
    two_gpu_dynamic_node, selector, drained_idx
):
    """Bitmap and count-only drains survive a non-clean controller restart.

    The drained GRES state is packed/unpacked with the node record (see
    gres_node_state_pack in src/common/node_conf.c), so it must be restored
    after the controller restarts and the node re-registers. The device is
    named by index or by UUID; a drain recorded against a UUID must be restored
    too.
    """
    node = two_gpu_dynamic_node(
        extra_conf="Gres=gpu:a100:2,bandwidth:lustre:4",
        gres_conf=(
            "Autodetect=full\n"
            "Name=gpu File=/dev/tty0 Type=a100\n"
            "Name=gpu File=/dev/tty1 Type=a100\n"
            "Name=bandwidth Type=lustre Count=4 Flags=CountOnly\n"
        ),
        uuids=(GPU_A, GPU_B),
    )
    result = atf.update_tres_drain(node, "gres/gpu", selector, reason="persist")
    assert result["exit_code"] == 0, result
    assert re.search(
        rf"DrainedTRES=.*gpu\(IDX:{drained_idx}\)\(persist\)",
        atf.get_node_detail(node),
    ), "DrainedTRES should show the drain"
    result = atf.update_tres_drain(
        node, "gres/bandwidth:lustre", "Count=2", reason="count_persist"
    )
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:lustre:2\(count_persist\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"

    atf.restart_slurmctld(clean=False, quiet=True)

    for _ in atf.timer(fatal=True):
        d = atf.get_node_detail(node)
        if re.search(
            rf"DrainedTRES=.*gpu\(IDX:{drained_idx}\)\(persist\)", d
        ) and re.search(r"bandwidth:lustre:2\(count_persist\)", d):
            break


def test_admin_drain_preserved_across_slurmd_reregistration(two_gpu_dynamic_node):
    """An admin GRES drain survives slurmd re-registration with all devices."""
    gres = "Gres=gpu:a100:2,bandwidth:alpha:2,bandwidth:zeta:2"
    node = two_gpu_dynamic_node(
        extra_conf=gres,
        gres_conf=(
            "Autodetect=full\n"
            "Name=gpu File=/dev/tty0 Type=a100\n"
            "Name=gpu File=/dev/tty1 Type=a100\n"
            "Name=bandwidth Type=alpha Count=2 Flags=CountOnly\n"
            "Name=bandwidth Type=zeta Count=2 Flags=CountOnly\n"
        ),
    )
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="admin")
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*gpu\(IDX:0\)\(admin\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"

    result = atf.update_tres_drain(
        node, "gres/bandwidth:alpha", "Count=1", reason="count_admin"
    )
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*bandwidth:alpha:1\(count_admin\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"

    # Keep the controller's node record so the new daemon performs a true
    # re-registration against the existing drained-TRES state.
    atf.stop_slurmd(node, quiet=True)

    atf.start_dynamic_slurmd(node, extra_conf=gres)

    assert re.search(
        r"DrainedTRES=.*gpu\(IDX:0\)\(admin\)", atf.get_node_detail(node)
    ), (
        "admin drain cleared after slurmd re-registration: "
        f"{atf.get_node_detail(node)!r}"
    )
    assert re.search(
        r"DrainedTRES=.*bandwidth:alpha:1\(count_admin\)", atf.get_node_detail(node)
    ), (
        "count-only admin drain cleared after re-registration: "
        f"{atf.get_node_detail(node)!r}"
    )
    assert "DEGRADED" in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should be DEGRADED"


def test_exclusive_whole_node_excludes_drained_gpu(two_gpu_dynamic_node):
    """An exclusive/whole-node job must not claim a drained-but-idle GPU.

    An exclusive job is given every GRES on the node without asking for it, so
    a drained GPU has to be left out of that whole-node allocation too, not
    only out of a request for a count.
    """
    node = two_gpu_dynamic_node(gres_conf=atf.gpu_gres_conf(DYN_NODE))

    # Drain idle GPU index 0 (no job is using it).
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", reason="test")
    assert result["exit_code"] == 0, result

    # An exclusive job auto-claims the node's whole GRES.
    job_id = atf.submit_job_sbatch(
        f"--exclusive -w {node} --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60)
    job = atf.get_job(job_id)
    # The drained device (index 0) must NOT be allocated; only index 1.
    assert "IDX:0" not in str(job), job
    assert re.search(r"gpu(?::a100)?:1\(IDX:1\)", str(job)), job


# ---------------------------------------------------------------------------
# Multinode GPU drain.
#
# The same drain seen by a job that spans nodes. DYN_NODE and DYN_NODE_B are
# non-adjacent in the cluster's node bitmap (DYN_NODE_SKIP is never targeted and
# DYN_NODE_GOOD sits between them), so a spanning allocation exercises the
# discontinuous-bitmap paths in the select plugin.
# ---------------------------------------------------------------------------


def test_gres_drain_multinode_symmetric_drain_reduces_per_node_capacity(
    dynamic_slurmds,
):
    """Symmetric GPU drain on the NODE_A/NODE_B pair blocks GPU jobs on that pair.

    Four nodes are registered: DYN_NODE_SKIP (index 0, first-choice bias guard),
    DYN_NODE (NODE_A, index 1), DYN_NODE_GOOD (index 2, healthy between A and B),
    DYN_NODE_B (NODE_B, index 3).  Each has one GPU (one TTY each, 4 TTYs total).

    Draining both NODE_A and NODE_B leaves 0 GPUs on each.  A 1-GPU-per-node job
    pinned to that discontinuous pair (node100, node102) stays PENDING; the same
    job pinned to the healthy pair (node99, node101) runs.  The NODE_GOOD between
    A and B ensures the job bitmap is discontinuous -- not just a contiguous range.
    """
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf((0, "(null)")))
    atf.restart_slurm(clean=True, quiet=True)
    node_skip = dynamic_slurmds(DYN_NODE_SKIP, f"Port={PORT_SKIP} Gres=gpu:a100:1")
    node_a = dynamic_slurmds(DYN_NODE, f"Port={PORT_A} Gres=gpu:a100:1")
    node_good = dynamic_slurmds(DYN_NODE_GOOD, f"Port={PORT_GOOD} Gres=gpu:a100:1")
    node_b = dynamic_slurmds(DYN_NODE_B, f"Port={PORT_B} Gres=gpu:a100:1")

    # Drain all GPUs on NODE_A and NODE_B; skip and good remain healthy.
    for node in (node_a, node_b):
        result = atf.update_tres_drain(
            node, "gres/gpu", "Index=ALL", reason="multinode"
        )
        assert result["exit_code"] == 0, f"drain on {node}: {result}"
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should be DEGRADED after GPU drain"

    job_drained = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_a},{node_b} --gpus-per-node=1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.job_stays_pending(
        job_drained
    ), "job on drained pair must stay PENDING: both nodes have 0 undrained GPUs"

    # Healthy pair (node99, node101): each has 1 GPU -> RUNNING.
    job_healthy = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_skip},{node_good} --gpus-per-node=1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_healthy, "RUNNING"
    ), "job on healthy pair must run: node99 and node101 have undrained GPUs"


def test_gres_drain_multinode_fully_drained_node_excluded_from_gres_job(
    dynamic_slurmds,
):
    """A fully-drained node cannot satisfy per-node GPU requests in a spanning job.

    Same 4-node layout as the symmetric test: DYN_NODE_SKIP (node99, index 0),
    DYN_NODE (node100/NODE_A, index 1), DYN_NODE_GOOD (node101, index 2),
    DYN_NODE_B (node102/NODE_B, index 3), each with 1 GPU.

    NODE_A's GPU is drained (0 available); NODE_B is healthy (1 available).
    A 2-node job pinned to the discontinuous pair (node100, node102) stays PENDING
    because node100 cannot contribute any GPU, even though node102 can.
    Resuming NODE_A's drain makes the pair viable and the job starts.
    """
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf((0, "(null)")))
    atf.restart_slurm(clean=True, quiet=True)

    dynamic_slurmds(DYN_NODE_SKIP, f"Port={PORT_SKIP} Gres=gpu:a100:1")
    node_a = dynamic_slurmds(DYN_NODE, f"Port={PORT_A} Gres=gpu:a100:1")
    dynamic_slurmds(DYN_NODE_GOOD, f"Port={PORT_GOOD} Gres=gpu:a100:1")
    node_b = dynamic_slurmds(DYN_NODE_B, f"Port={PORT_B} Gres=gpu:a100:1")

    # Drain NODE_A's only GPU; node_skip, node_good, and node_b are untouched.
    result = atf.update_tres_drain(node_a, "gres/gpu", "Index=ALL", reason="full-drain")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        node_a, "DEGRADED"
    ), f"{node_a} should be DEGRADED after draining its only GPU"
    assert "DEGRADED" not in (
        atf.get_node_parameter(node_b, "state") or ""
    ), f"{node_b} must not be DEGRADED -- only {node_a} was drained"

    job_id = atf.submit_job_sbatch(
        f"--nodes=2 -w {node_a},{node_b} --gpus-per-node=1 --wrap='sleep infinity'",
        fatal=True,
    )
    assert atf.job_stays_pending(
        job_id
    ), f"job must stay PENDING: {node_a} has 0 undrained GPUs"

    # Resume NODE_A's drain: both nodes now have 1 GPU each -> job starts.
    result = atf.update_tres_drain(node_a, "gres/gpu", "Index=ALL", action="Resume")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "job should start once NODE_A's GPU drain is resumed"


def test_gres_drain_multinode_invalid_selector_changes_nothing(dynamic_slurmds):
    """A selector invalid on one target makes the multi-node update atomic."""
    atf.require_config_file(
        "gres.conf",
        "Autodetect=full\n"
        f"NodeName={DYN_NODE} Name=gpu File=/dev/tty0 Type=a100\n"
        f"NodeName={DYN_NODE} Name=gpu File=/dev/tty1 Type=a100\n"
        f"NodeName={DYN_NODE_B} Name=gpu File=/dev/tty0 Type=a100\n",
    )
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, "(null)"), (1, "(null)"))
    )
    atf.restart_slurm(clean=True, quiet=True)
    node_a = dynamic_slurmds(DYN_NODE, f"Port={PORT_A} Gres=gpu:a100:2")
    node_b = dynamic_slurmds(DYN_NODE_B, f"Port={PORT_B} Gres=gpu:a100:1")

    result = atf.update_tres_drain(
        f"{node_a},{node_b}", "gres/gpu", "Index=1", reason="atomic"
    )
    assert result["exit_code"] != 0, result
    assert "Invalid generic resource (gres) specification" in result["stderr"], result
    assert (
        atf.get_node_tres_drained(node_a) is None
    ), "node should report no drained TRES"
    assert (
        atf.get_node_tres_drained(node_b) is None
    ), "node should report no drained TRES"


# ---------------------------------------------------------------------------
# Drain GPUs by UUID.
#
# A present GPU can be selected for drain or resume by its UUID. These tests
# drain present devices by UUID, reject a UUID no device has, and reject a
# UUID named as the wrong type.
#
# An index shift when the configured set shrinks is test_190_14. An absent
# index is test_190_13. Neither fits these dynamic-normal (`slurmd -Z`) nodes:
# the controller rejects a live GRES reconfig of an existing dynamic node as
# INVALID_REG.
# ---------------------------------------------------------------------------


def test_gres_drain_multiple_uuids(dynamic_slurmds):
    """A comma-separated UUID list drains each named device."""
    atf.require_config_file(
        "fake_gpus.conf",
        atf.fake_gpus_conf((0, GPU_A), (1, GPU_B), (2, GPU_C)),
    )
    atf.restart_slurm(clean=True, quiet=True)

    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:3")
    result = atf.update_tres_drain(
        node, "gres/gpu", f"Uuid={GPU_A},{GPU_C}", reason="two"
    )
    assert result["exit_code"] == 0, result

    detail = atf.get_node_detail(node)
    # Indexes 0 and 2 share a reason, so they group: gpu(IDX:0,2)(two).
    assert re.search(r"DrainedTRES=.*gpu\(IDX:0,2\)\(two\)", detail), detail


@pytest.mark.parametrize(
    "uuid", ["GPU-does-not-exist", GPU_A.lower()], ids=["unknown", "lowercase"]
)
def test_gres_drain_unknown_uuid_rejected(two_gpu_dynamic_node, uuid):
    """Draining a UUID no device has is rejected and leaves the node unchanged.

    UUIDs match case-sensitively, so a device's UUID in lower case names no
    device either.
    """
    node = two_gpu_dynamic_node(uuids=(GPU_A, GPU_B))
    result = atf.update_tres_drain(node, "gres/gpu", f"Uuid={uuid}", reason="nope")
    assert result["exit_code"] != 0, result
    assert "Requested GRES UUID not found on node" in result["stderr"], result

    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)
    assert "DEGRADED" not in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should not be DEGRADED"


def test_gres_drain_uuid_type_mismatch_rejected(dynamic_slurmds):
    """A typed UUID drain must match the device's type; a bare one need not."""
    atf.require_config_file(
        "fake_gpus.conf",
        atf.fake_gpus_conf((0, "a100", GPU_A), (1, "v100", GPU_B)),
    )
    atf.restart_slurm(clean=True, quiet=True)

    node = dynamic_slurmds(DYN_NODE, extra_conf="Gres=gpu:a100:1,gpu:v100:1")

    # GPU_A is an a100, so requesting it as a v100 is rejected.
    result = atf.update_tres_drain(
        node, "gres/gpu:v100", f"Uuid={GPU_A}", reason="wrong"
    )
    assert result["exit_code"] != 0, result
    assert "Invalid GRES specification" in result["stderr"], result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)

    # Without a type qualifier the same UUID drains fine.
    result = atf.update_tres_drain(node, "gres/gpu", f"Uuid={GPU_A}", reason="ok")
    assert result["exit_code"] == 0, result
    assert re.search(
        r"DrainedTRES=.*gpu\(IDX:0\)\(ok\)", atf.get_node_detail(node)
    ), "DrainedTRES should show the drain"


def test_gres_drain_by_conf_uuid(two_gpu_dynamic_node):
    """A UUID set by the gres.conf UUID option works as a drain selector.

    Every other UUID test here gets its UUIDs from AutoDetect. This one turns
    AutoDetect off entirely, so the only place the UUID can have come from is
    the UUID option -- which is the point of that option: it lets a site
    anchor a drain to a physical device without vendor libraries on the node.
    """
    node = two_gpu_dynamic_node(
        gres_conf=(
            f"NodeName={DYN_NODE} AutoDetect=off "
            f"Name=gpu File=/dev/tty0 Type=a100 UUID={GPU_A}\n"
            f"NodeName={DYN_NODE} AutoDetect=off "
            f"Name=gpu File=/dev/tty1 Type=a100 UUID={GPU_B}\n"
        )
    )

    result = atf.update_tres_drain(
        node, "gres/gpu", f"Uuid={GPU_B}", reason="conf_uuid"
    )
    assert result["exit_code"] == 0, result

    # The drain lands on the device the UUID option names, so it is reported
    # at that device's index.
    detail = atf.get_node_detail(node)
    assert re.search(r"DrainedTRES=.*gpu\(IDX:1\)\(conf_uuid\)", detail), detail
    assert "IDX:0" not in detail, detail


def test_uuid_drain_of_non_gpu_gres(dynamic_slurmds):
    """Uuid= selects a nic the way it selects a GPU, and only that device."""
    atf.require_config_file(
        "gres.conf",
        "AutoDetect=off\n"
        f"NodeName={DYN_NODE} Name=nic File=/dev/tty0 UUID={NIC_A}\n"
        f"NodeName={DYN_NODE} Name=nic File=/dev/tty1 UUID={NIC_B}\n",
    )
    atf.restart_slurm(clean=True, quiet=True)
    node = dynamic_slurmds(DYN_NODE, "Gres=nic:2")

    atf.update_tres_drain(node, "gres/nic", f"Uuid={NIC_A}", reason="link", fatal=True)
    detail = atf.get_node_detail(node)
    assert re.search(r"DrainedTRES=.*nic\(IDX:0\)\(link\)", detail), detail
    drained = atf.get_node_tres_drained(node) or ""
    assert "IDX:1" not in drained, drained
    assert atf.wait_for_node_state(node, "DEGRADED"), detail
