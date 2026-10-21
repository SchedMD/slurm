############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Per-node SuspendTime when automatic power save is not configured.

With SuspendProgram and ResumeProgram unset, slurmctld rejects a finite
SuspendTime on node creation and any SuspendTime on a node update, while a
dynamic node registering with 'slurmd -Z' is accepted with a warning.
"""

import pytest

import atf

max_nodes = 5
dyn_node = "node100"

g_suspend_time = 100
n_suspend_time = 109

infinite_option_num = 4294967295


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("MaxNodeCount", max_nodes)
    atf.require_config_parameter("SuspendProgram", None)
    atf.require_config_parameter("ResumeProgram", None)
    atf.require_config_parameter("SuspendTime", g_suspend_time)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")

    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def cleanup():
    yield

    atf.run_command(
        "scontrol delete nodename=d1",
        user=atf.properties["slurm-user"],
    )
    # d2 is left behind if an assertion fails before the test deletes it.
    atf.run_command(
        "scontrol delete nodename=d2",
        user=atf.properties["slurm-user"],
    )
    atf.stop_slurmd(dyn_node)
    atf.run_command(
        f"scontrol delete nodename={dyn_node}",
        user=atf.properties["slurm-user"],
    )


def suspend_time_val(value):
    """Build expected suspend_time no_val_struct dict for node comparison."""
    if value == infinite_option_num:
        return {"set": False, "infinite": True, "number": 0}
    return {"set": True, "infinite": False, "number": value}


@pytest.mark.skipif(
    atf.get_version() < (26, 11),
    reason=(
        "#50873: rejecting node creation with SuspendTime when power save"
        " is disabled added in 26.11"
    ),
)
@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 5),
    reason="Ticket 24669: Per-node SuspendTime added in 26.05",
)
def test_create_node_suspend_time_power_save_disabled():
    # Creating without SuspendTime must succeed even when power save is disabled
    atf.run_command(
        "scontrol create nodename=d1 state=cloud",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Creating with a finite SuspendTime must fail when power save is disabled
    result = atf.run_command(
        f"scontrol create nodename=d2 state=cloud SuspendTime={n_suspend_time}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )
    assert "automatic power save mode is disabled" in result["stderr"], (
        "Node creation with a finite SuspendTime should be rejected while"
        f" power save is disabled, got: {result['stderr']}"
    )
    assert "d2" not in atf.get_nodes(), "A rejected node creation should not create d2"

    # INFINITE, NONE and -1 are accepted on create even when power save is
    # disabled
    for alias in ("INFINITE", "NONE", "-1"):
        atf.run_command(
            f"scontrol create nodename=d2 state=cloud SuspendTime={alias}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        assert atf.get_nodes()["d2"]["suspend_time"] == suspend_time_val(
            infinite_option_num
        ), f"Creating d2 with SuspendTime={alias} should disable suspending it"
        atf.run_command(
            "scontrol delete nodename=d2",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason=(
        "#50873: rejecting node SuspendTime updates when power save"
        " is disabled added in 26.11"
    ),
)
def test_update_node_suspend_time_power_save_disabled():
    atf.run_command(
        "scontrol create nodename=d1 state=cloud",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    nodes = atf.get_nodes()
    before = nodes["d1"]["suspend_time"]
    assert before == suspend_time_val(
        g_suspend_time
    ), "d1 should inherit the finite global SuspendTime"

    # Unlike creation, the update path also rejects INFINITE/NONE/-1
    for value in (n_suspend_time, "INFINITE", "NONE", "-1"):
        result = atf.run_command(
            f"scontrol update nodename=d1 SuspendTime={value}",
            user=atf.properties["slurm-user"],
            xfail=True,
            fatal=True,
        )
        assert (
            "Requested operation is presently disabled" in result["stderr"]
        ), f"SuspendTime={value} update should fail with ESLURM_DISABLED"
        nodes = atf.get_nodes()
        assert (
            nodes["d1"]["suspend_time"] == before
        ), f"SuspendTime={value} update should have been rejected"

    # A rejected SuspendTime must not apply other fields in the same request
    result = atf.run_command(
        f"scontrol update nodename=d1 SuspendTime={n_suspend_time}"
        " Weight=7 Comment=rejected",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )
    assert (
        "Requested operation is presently disabled" in result["stderr"]
    ), "SuspendTime update with other fields should fail with ESLURM_DISABLED"
    d1 = atf.get_nodes()["d1"]
    assert d1["weight"] == 1, "A rejected update should not change Weight"
    assert d1["comment"] == "", "A rejected update should not change Comment"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason=(
        "#50873: accepting dynamic node SuspendTime with a warning when power"
        " save is disabled added in 26.11"
    ),
)
@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 5)
    or atf.get_version("sbin/slurmd") < (26, 5),
    reason="Ticket 24669: Per-node SuspendTime added in 26.05",
)
def test_dynamic_register_suspend_time_power_save_disabled():
    """slurmd -Z with a finite SuspendTime registers when power save is disabled"""
    atf.start_slurmd(
        dyn_node, f"-Z -b --conf 'SuspendTime={n_suspend_time}'", wait="idle"
    )
    assert atf.get_nodes()[dyn_node]["suspend_time"] == suspend_time_val(
        n_suspend_time
    ), "The dynamic node should keep the SuspendTime from --conf"

    warning = (
        f"warning: Dynamic node {dyn_node} registered with"
        f" SuspendTime={n_suspend_time},"
        " but automatic power save mode is disabled"
    )
    logfile = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    assert atf.run_command_output(
        f"grep -F -- '{warning}' {logfile}", user=atf.properties["slurm-user"]
    ), "slurmctld should log a warning for the registered SuspendTime"
