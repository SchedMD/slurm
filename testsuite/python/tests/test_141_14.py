############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Power save activates from a per-node SuspendTime with no reconfigure.

Automatic power save is enabled when at least one node has a finite
SuspendTime and valid SuspendProgram/ResumeProgram are configured. The
power-save thread must start acting on a node the moment it gets a finite
SuspendTime through ``scontrol create``/``scontrol update`` -- without
requiring a ``scontrol reconfig`` to refresh the enabled flag.

This is exercised with the global SuspendTime set to INFINITE (so power save
is disabled at startup): once for a static node given a finite per-node
SuspendTime with ``scontrol update``, and once for a dynamic node created
with a finite SuspendTime via ``scontrol create``. In both cases the node
must power up for a job, go IDLE, and then suspend on the per-node schedule,
all with no reconfigure in between.
"""

import time

import pytest

import atf

pytestmark = pytest.mark.slow

node_name = "node1"
suspend_time = 10
suspend_timeout = 10
resume_timeout = 10
max_nodes = 5
# The power-save thread only rescans every power_save_interval, so a node can
# suspend up to one interval after suspend_time elapses.
power_save_interval = 10


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")

    # Global SuspendTime INFINITE so automatic power save is disabled at
    # startup; the per-node SuspendTime set later is what must enable it.
    atf.require_config_parameter("SuspendTime", "INFINITE")
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    atf.require_config_parameter_includes("SlurmctldParameters", "idle_on_node_suspend")
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("power_save_interval", power_save_interval)
    )
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("power_save_min_interval", 0)
    )
    # Needed for the dynamic-node create-path test below.
    atf.require_config_parameter("MaxNodeCount", max_nodes)

    # A static cloud node registered in slurm.conf so a slurmd can register
    # against it; the per-node SuspendTime is applied at runtime below.
    atf.require_config_parameter("NodeName", {node_name: {"State": "CLOUD"}})
    atf.require_config_parameter("PartitionName", {"cloud1": {"Nodes": "ALL"}})

    # Don't run atf.require_slurm_running(): tests start slurmds manually.
    atf.start_slurmctld(clean=True)

    yield

    atf.stop_slurmctld(also_slurmds=True)


@pytest.fixture(autouse=True)
def reset_state():
    yield
    kill_slurmds()
    atf.restart_slurmctld(clean=True)
    atf.wait_for_node_state(node_name, "POWERED_DOWN", fatal=True)


def kill_slurmds():
    result = atf.run_command(f"pidof {atf.properties['slurm-sbin-dir']}/slurmd")
    for pid in result["stdout"].strip().split():
        atf.run_command(f"kill {pid}", fatal=True, user="root")


def _power_up_and_idle(target=node_name, dynamic=False):
    """Power the given cloud node up through a job and return to IDLE.

    dynamic selects '-Z' registration for a node created via 'scontrol
    create' rather than declared on a NodeName line in slurm.conf -- without
    it, slurmd looks up the name in its own slurm.conf-derived table and
    fails to find it.

    Returns the job id. Asserts the node reaches IDLE so the idle clock starts.
    """
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 -w {target} --wrap 'srun hostname'", fatal=True
    )
    atf.wait_for_node_state(target, "ALLOCATED", fatal=True)
    atf.wait_for_node_state(target, "POWERING_UP", fatal=True)

    # TODO: Wait 2 seconds to avoid race condition between slurmd and
    #       slurmctld. Remove once bug 16459 is fixed.
    time.sleep(2)

    dynamic_flag = " -Z" if dynamic else ""
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {target}{dynamic_flag}",
        fatal=True,
        user="root",
    )

    atf.wait_for_node_state(target, "POWERING_UP", reverse=True, fatal=True)
    atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=atf.PERIODIC_TIMEOUT + 15, fatal=True
    )
    assert "IDLE" in atf.get_node_parameter(target, "state"), (
        "With idle_on_node_suspend, the cloud node should be IDLE once the"
        " job completes"
    )
    return job_id


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="#50873: per-node SuspendTime enabling power save at runtime",
)
def test_per_node_suspend_time_enables_power_save_no_reconfig():
    assert "POWERED_DOWN" in atf.get_node_parameter(
        node_name, "state"
    ), "Cloud node must start in POWERED_DOWN state"

    # Give the node a finite per-node SuspendTime at runtime. This is the
    # change that must enable automatic power save without a reconfigure.
    atf.run_command(
        f"scontrol update nodename={node_name} SuspendTime={suspend_time}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert (
        atf.get_node_parameter(node_name, "suspend_time")["number"] == suspend_time
    ), "Per-node SuspendTime update was not applied"

    # Power up, run a job, and return to IDLE so the idle clock starts. No
    # scontrol reconfig is issued anywhere in this test.
    _power_up_and_idle()

    # The node must suspend on the per-node SuspendTime schedule, proving the
    # power-save thread is acting on the runtime per-node value with no
    # reconfigure.
    atf.wait_for_node_state(node_name, ["POWER_DOWN", "POWERING_DOWN"], fatal=True)
    atf.wait_for_node_state(node_name, "POWERED_DOWN", fatal=True)


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="#50873: per-node SuspendTime enabling power save at runtime",
)
@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 5),
    reason="Ticket 24669: Per-node SuspendTime added in 26.05",
)
def test_dynamic_node_create_suspend_time_enables_power_save_no_reconfig():
    """The create path, not just update, must enable power save at runtime.

    Mirrors test_per_node_suspend_time_enables_power_save_no_reconfig, but
    the finite per-node SuspendTime is supplied by 'scontrol create' for a
    brand new dynamic node instead of 'scontrol update' on an existing one --
    the path a cloud/autoscaling workflow actually uses.
    """
    dynamic_node = "d1"

    atf.run_command(
        f"scontrol create nodename={dynamic_node} state=cloud"
        f" SuspendTime={suspend_time}",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    assert (
        atf.get_node_parameter(dynamic_node, "suspend_time")["number"] == suspend_time
    ), "Per-node SuspendTime set at create time was not applied"
    assert "POWERED_DOWN" in atf.get_node_parameter(
        dynamic_node, "state"
    ), "Newly created cloud node must start in POWERED_DOWN state"

    # Power up, run a job, and return to IDLE so the idle clock starts. No
    # scontrol reconfig is issued anywhere in this test.
    _power_up_and_idle(dynamic_node, dynamic=True)

    # The node must suspend on the per-node SuspendTime schedule, proving the
    # power-save thread acted on the value from 'scontrol create' with no
    # reconfigure.
    atf.wait_for_node_state(dynamic_node, ["POWER_DOWN", "POWERING_DOWN"], fatal=True)
    atf.wait_for_node_state(dynamic_node, "POWERED_DOWN", fatal=True)
