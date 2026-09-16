############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Regression: a POWERED_DOWN cloud node must not be left NOT_RESPONDING.

A powered down node has no slurmd to answer, and ping_nodes() skips it, so a
NODE_STATE_NO_RESPOND flag set on it is never cleared. _build_bitmaps() will
not put a NO_RESPOND node into avail_node_bitmap, so after the next
reconfigure or restart the node is permanently unschedulable -- and a cloud
node that is never scheduled is never resumed.

Ticket 25110.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

node_name = "node1"
suspend_time = 10
suspend_timeout = 10
resume_timeout = 10
power_save_interval = 10
# check_node_timers(), which fires a scheduled ResumeAfter, runs on the
# controller's 30s PERIODIC_TIMEOUT tick, so a resumeafter=1 can take a full
# tick to land on top of the usual scheduling latency.
resume_after_timeout = 90


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5, 5),
        "sbin/slurmctld",
        reason="Ticket 25110: _require_node_reg() sets NO_RESPOND on"
        " POWERED_DOWN nodes before 26.05.5",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TreeWidth", 65533)
    # /bin/true never actually brings the node up, so the resume always
    # fails and the node falls back to POWERED_DOWN at ResumeTimeout.
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    # The waits below are budgeted against this scan interval, so pin it
    # rather than inheriting whatever the installation configures.
    atf.require_config_parameter_includes(
        "SlurmctldParameters", f"power_save_interval={power_save_interval}"
    )
    atf.require_config_parameter("NodeName", {node_name: {"State": "CLOUD"}})
    atf.require_config_parameter(
        "PartitionName", {"primary": {"Nodes": "ALL", "Default": "YES"}}
    )

    atf.start_slurmctld(clean=True)

    yield

    # conftest only cancels jobs and stops daemons when it started Slurm
    # itself (it keys off properties["slurm-started"], which
    # require_slurm_running() sets and start_slurmctld() does not), so do both
    # here. Otherwise this module's slurmctld outlives it and every later
    # module inherits its cloud-node config.
    atf.cancel_all_jobs(fatal=True, quiet=True)
    atf.stop_slurmctld()


@pytest.fixture(scope="function")
def powered_down_node():
    """Drive the cloud node to DOWN+POWERED_DOWN via a failed resume."""
    # The node starts out IDLE+POWERED_DOWN, so wait on DOWN rather than
    # POWERED_DOWN: only the failed resume can put it DOWN.
    atf.run_command("sbatch --wrap 'hostname'", fatal=True)
    # The failed resume is quantized by power_save_interval (10s by default)
    # at both ends: one tick to issue the resume, ResumeTimeout to expire,
    # then another tick to notice it and mark the node DOWN. Leave generous
    # room on top of that for a loaded CI box.
    atf.wait_for_node_state(node_name, "DOWN", timeout=resume_timeout + 80, fatal=True)
    st = atf.get_node_parameter(node_name, "state")
    assert "POWERED_DOWN" in st, (
        f"Setup error: cloud {node_name} should be POWERED_DOWN after a "
        f"failed resume; state={st!r}"
    )
    # As in the module teardown, conftest will not cancel for us because this
    # module starts slurmctld itself, so these are load-bearing rather than
    # belt-and-braces: a leftover job would resume the node under the next test.
    atf.cancel_all_jobs(fatal=True, quiet=True)

    yield node_name

    atf.cancel_all_jobs(fatal=True, quiet=True)
    atf.restart_slurmctld(clean=True)


@pytest.mark.parametrize("resume_via", ["state", "resumeafter"])
def test_resume_powered_down_no_not_responding(powered_down_node, resume_via):
    """resume on a POWERED_DOWN cloud node must not set NOT_RESPONDING."""
    node = powered_down_node
    slurm_user = atf.properties["slurm-user"]

    if resume_via == "state":
        atf.run_command(
            f"scontrol update nodename={node} state=resume",
            user=slurm_user,
            fatal=True,
        )
    else:
        # The sequence reported in ticket 25110: a deferred resume scheduled
        # with ResumeAfter. It lands on the same NODE_RESUME handling, but
        # through check_node_timers() rather than the update RPC.
        atf.run_command(
            f"scontrol update nodename={node} state=drain resumeafter=1"
            f" reason=test_141_6",
            user=slurm_user,
            fatal=True,
        )
    # Longer than the default: see resume_after_timeout above, the deferred
    # resume waits on the controller's 30s periodic tick.
    atf.wait_for_node_state(node, "IDLE", timeout=resume_after_timeout, fatal=True)

    st = atf.get_node_parameter(node, "state")
    assert "POWERED_DOWN" in st, (
        f"resume must not power up cloud {node}; the NOT_RESPONDING check "
        f"below is only meaningful while it is still POWERED_DOWN; "
        f"state={st!r}"
    )
    assert "NOT_RESPONDING" not in st, (
        f"POWERED_DOWN cloud {node} must not be NOT_RESPONDING after resume; "
        f"state={st!r}"
    )


def test_powered_down_schedulable_after_reconfigure(powered_down_node):
    """A resumed cloud node must still be schedulable after a reconfigure."""
    node = powered_down_node
    slurm_user = atf.properties["slurm-user"]

    atf.run_command(
        f"scontrol update nodename={node} state=resume",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command("scontrol reconfigure", user=slurm_user, fatal=True)
    # Wait for the re-exec'd slurmctld to answer again. get_node_parameter()
    # pytest.fail()s with a gcore dump if it cannot reach the controller, so
    # racing it here would be reported as a crash rather than as a race.
    assert atf.repeat_command_until(
        "scontrol ping", lambda results: "is UP" in results["stdout"]
    ), "slurmctld did not come back up after scontrol reconfigure"

    st = atf.get_node_parameter(node, "state")
    assert "POWERED_DOWN" in st, (
        f"cloud {node} must still be POWERED_DOWN here; the wait below "
        f"detects it leaving that state, so it would pass without waiting "
        f"if it had already left; state={st!r}"
    )
    assert "NOT_RESPONDING" not in st, (
        f"POWERED_DOWN cloud {node} must not be NOT_RESPONDING after "
        f"reconfigure; state={st!r}"
    )

    # The node must be picked up by the scheduler again, which for a cloud
    # node means a resume is issued and it leaves POWERED_DOWN.
    atf.run_command("sbatch --wrap 'hostname'", fatal=True)
    # Longer than the default: the scheduler has to run and then power_save
    # has to issue the resume on its own power_save_interval (10s) tick.
    assert atf.wait_for_node_state(node, "POWERED_DOWN", reverse=True, timeout=60), (
        f"Cloud {node} was never scheduled after reconfigure, so it was never "
        f"resumed; state={atf.get_node_parameter(node, 'state')!r}"
    )


def test_powered_down_not_responding_cleared_on_restart(powered_down_node):
    """A NOT_RESPONDING flag on a powered down node must not survive a restart.

    This covers the load_all_node_state() half of the fix, the half that heals
    nodes an older slurmctld already left stuck. With the guard in
    _require_node_reg() in place the flag can no longer be set through the
    resume path, so set it explicitly with the documented State=NoResp action,
    which writes the flag directly and is unaffected by the guard.
    """
    node = powered_down_node
    slurm_user = atf.properties["slurm-user"]

    atf.run_command(
        f"scontrol update nodename={node} state=NoResp",
        user=slurm_user,
        fatal=True,
    )
    st = atf.get_node_parameter(node, "state")
    assert (
        "NOT_RESPONDING" in st
    ), f"Setup error: State=NoResp should have flagged {node}; state={st!r}"
    assert (
        "POWERED_DOWN" in st
    ), f"Setup error: cloud {node} should still be POWERED_DOWN; state={st!r}"

    # Restart without clean=True so slurmctld recovers the node state file and
    # load_all_node_state() actually runs. clean=True passes -c -i, which is
    # recover=0, and would skip the recovery path entirely.
    atf.restart_slurmctld()

    st = atf.get_node_parameter(node, "state")
    assert "NOT_RESPONDING" not in st, (
        f"POWERED_DOWN cloud {node} must not still be NOT_RESPONDING after a "
        f"slurmctld restart recovered its state; state={st!r}"
    )
