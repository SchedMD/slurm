############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Per-node SuspendTime across reconfigure and slurmctld restarts.

A SuspendTime set with scontrol update on a node defined in slurm.conf
survives a full state recovery (slurmctld -R), but scontrol reconfigure and a
plain restart reset it to the configured value. A dynamic node keeps the
value across restarts.
"""

import pytest

import atf

max_nodes = 5

g_suspend_time = 100
g_suspend_timeout = 101
g_resume_timeout = 102

p1_suspend_time = 103

n_update_suspend_time = 110


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Two static nodes, a global SuspendTime and p1 with its own one."""
    atf.require_nodes(2, [("CPUs", 4), ("RealMemory", 40)])
    atf.require_config_parameter_includes("ReconfigFlags", "KeepPartInfo")
    atf.require_config_parameter_includes("ReconfigFlags", "KeepPartState")
    atf.require_config_parameter("MaxNodeCount", max_nodes)
    # Needed for MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", g_suspend_time)
    atf.require_config_parameter("SuspendTimeout", g_suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", g_resume_timeout)
    atf.require_config_parameter(
        "PartitionName",
        {
            "all": {"Nodes": "ALL"},
            "p1": {"SuspendTime": p1_suspend_time},
        },
    )

    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def cleanup():
    yield

    atf.run_command(
        "scontrol delete nodename=d1",
        user=atf.properties["slurm-user"],
    )


def suspend_time_val(value):
    """Build expected suspend_time no_val_struct dict for node comparison."""
    return {"set": True, "infinite": False, "number": value}


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="#50873: per-node SuspendTime updates added in 26.11",
)
def test_update_node_suspend_time_full_state_recovery():
    """A scontrol-set per-node SuspendTime must survive slurmctld -R, while a
    node never set explicitly keeps inheriting the config (nothing pinned)."""

    def suspend_time(node):
        return atf.get_nodes()[node]["suspend_time"]

    # node1 gets an explicit per-node value at runtime; node2 is left
    # untouched and inherits the global SuspendTime.
    atf.run_command(
        f"scontrol update nodename=node1 SuspendTime={n_update_suspend_time}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert suspend_time("node1") == suspend_time_val(
        n_update_suspend_time
    ), "The per-node SuspendTime update should be applied"
    assert suspend_time("node2") == suspend_time_val(
        g_suspend_time
    ), "Untouched node2 should inherit the global SuspendTime"

    atf.restart_slurmctld(full_recovery=True)

    # The explicitly set value survives -R; the untouched node2 is unchanged.
    assert suspend_time("node1") == suspend_time_val(
        n_update_suspend_time
    ), "scontrol-set SuspendTime should survive slurmctld -R"
    assert suspend_time("node2") == suspend_time_val(
        g_suspend_time
    ), "Untouched node2 should still inherit the global SuspendTime after -R"

    # And nothing may be pinned into node2's config record: a partition-level
    # SuspendTime must still win for it afterwards.
    atf.run_command(
        "scontrol update partitionname=p1 nodes=node2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert suspend_time("node2") == suspend_time_val(
        p1_suspend_time
    ), "node2 should follow the partition SuspendTime (nothing pinned on -R)"


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="#50873: per-node SuspendTime updates added in 26.11",
)
def test_update_node_suspend_time_reset_on_reconfigure():
    """scontrol reconfigure resets a slurm.conf node to its configured value"""
    atf.run_command(
        f"scontrol update nodename=node1 SuspendTime={n_update_suspend_time}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.get_nodes()["node1"]["suspend_time"] == suspend_time_val(
        n_update_suspend_time
    ), "The per-node SuspendTime update should be applied"

    atf.run_command(
        "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
    )
    assert atf.get_nodes()["node1"]["suspend_time"] == suspend_time_val(
        g_suspend_time
    ), "scontrol reconfigure should reset node1 to the global SuspendTime"


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="#50873: per-node SuspendTime updates added in 26.11",
)
def test_update_node_suspend_time_reset_on_restart():
    """A plain slurmctld restart resets a slurm.conf node to its configured value"""
    atf.run_command(
        f"scontrol update nodename=node1 SuspendTime={n_update_suspend_time}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.get_nodes()["node1"]["suspend_time"] == suspend_time_val(
        n_update_suspend_time
    ), "The per-node SuspendTime update should be applied"

    atf.restart_slurmctld()
    assert atf.get_nodes()["node1"]["suspend_time"] == suspend_time_val(
        g_suspend_time
    ), "A plain restart should reset node1 to the global SuspendTime"


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="#50873: per-node SuspendTime updates added in 26.11",
)
def test_dynamic_node_suspend_time_survives_restart():
    """A dynamic node keeps an updated SuspendTime across a plain restart"""
    atf.run_command(
        "scontrol create nodename=d1 state=cloud",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol update nodename=d1 SuspendTime={n_update_suspend_time}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert atf.get_nodes()["d1"]["suspend_time"] == suspend_time_val(
        n_update_suspend_time
    ), "The per-node SuspendTime update should be applied"

    atf.restart_slurmctld()
    assert atf.get_nodes()["d1"]["suspend_time"] == suspend_time_val(
        n_update_suspend_time
    ), "A dynamic node's SuspendTime should survive a plain restart"
