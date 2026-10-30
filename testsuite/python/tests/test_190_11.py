############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""An untracked GPU drain opens no accounting event.

gres/gpu is not in AccountingStorageTRES by default. Draining one still
marks the node DEGRADED, and sacctmgr records no drained-TRES event for it.
Once a tracked TRES is drained too, the event counts only the tracked one,
but its Reason and TresReference still name the untracked GPU.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node100"
PORT = 61622
REASON = "untracked_gpu"
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/sacctmgr",
        reason="Ticket 25180: drained-TRES events added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_accounting()
    atf.require_config_parameter_excludes("AccountingStorageTRES", "gres/gpu")
    atf.require_config_parameter("MaxNodeCount", 4)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_tty(0)
    atf.require_slurm_running()


def _one_gpu_node(dynamic_slurmds):
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A)))
    atf.restart_slurm(clean=True, quiet=True)
    dynamic_slurmds(NODE, f"Gres=gpu:a100:1 Port={PORT}")


def test_untracked_gpu_drain_opens_no_event(dynamic_slurmds):
    """Draining gres/gpu with tracking left off records no event for that drain."""
    _one_gpu_node(dynamic_slurmds)

    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason=REASON, fatal=True)
    assert atf.wait_for_node_state(
        NODE, "DEGRADED"
    ), "the node should be DEGRADED even though the GPU is not tracked"

    # Once slurmctld has sent slurmdbd everything queued, any event the
    # drain opened would be in the database.
    atf.wait_for_dbd_agent_drained(fatal=True)
    events = atf.get_node_events(NODE) or ""
    assert REASON not in events, events

    # States=Degraded selects only a drained-TRES event. An untracked GPU
    # must not open one, even under some other reason.
    degraded = atf.run_command_output(
        f"sacctmgr -n -P list events all_time Event=Node Nodes={NODE} "
        "States=Degraded format=nodename,reason,tres,state",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    assert not (
        degraded or ""
    ).strip(), f"an untracked GPU drain opened a drained-TRES event:\n{degraded}"


def test_event_names_untracked_gpu_beside_tracked_cpu(dynamic_slurmds):
    """An event opened by a tracked drain still names the untracked one.

    CPUs are always tracked, so draining one opens the event. Only the CPU is
    counted in its TRES, but its Reason and TresReference list the GPU too.
    """
    _one_gpu_node(dynamic_slurmds)

    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="gpu_why", fatal=True)
    atf.update_tres_drain(NODE, "cpu", "Index=0", reason="cpu_why", fatal=True)

    for _ in atf.timer(fatal=True):
        reason = atf.get_open_node_event_reason(NODE)
        if reason == "cpu_why,gpu_why":
            break

    tres = atf.get_open_node_event_tres(NODE) or ""
    assert re.search(
        r"(?:^|,)cpu=1(?:,|$)", tres
    ), f"the drained CPU should be counted: {atf.get_node_events(NODE)!r}"
    assert (
        "gres/gpu" not in tres
    ), f"the untracked GPU should not be counted: {atf.get_node_events(NODE)!r}"
    assert atf.get_open_node_event_tres_reference(NODE) == (
        f"cpu(IDX:0),gres/gpu:a100(IDX:0)(UUID:{GPU_A})"
    ), f"the event should name both TRES: {atf.get_node_events(NODE)!r}"
