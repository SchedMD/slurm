############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""scontrol reboot: cancel_reboot drain handling, nextstate=resume, timeout.

cancel_reboot undoes only what "scontrol reboot ASAP" did: a drain the admin
set before the request, with its reason, survives the cancel. A reboot with
nextstate=resume clears the drain, and from 26.05 on the reason, once the
node registers after the reboot. A reboot that never completes marks the node DOWN with
"reboot timed out" after ResumeTimeout and keeps an admin drain. A node that
reboots on its own while a plain reboot is pending also keeps the drain.
"""

import time

import pytest

import atf

pytestmark = pytest.mark.slow

node = None
resume_timeout = 30
# Releases before 26.05 check ResumeTimeout only every 30 seconds, so a
# timed-out reboot can take up to resume_timeout + 30 seconds to be noticed
timeout_wait = resume_timeout + 60
complete_flag = None
slurm_user = None


def reboot_script(path):
    """RebootProgram, run by slurmd: complete the reboot only when asked.

    Without the flag file the program does nothing and the node stays
    REBOOT_ISSUED until ResumeTimeout. With it, slurmd is stopped and started
    again with -b, which reports a new boot time like a real reboot would.
    The node name and pidfile are written into the script because slurmd
    exports SLURM_NODE_NAME to RebootProgram only from 26.05 on.
    """
    slurmd = atf.properties["slurm-sbin-dir"] + "/slurmd"
    pidfile = atf.get_config_parameter("SlurmdPidFile", live=False, quiet=True)
    pidfile = (pidfile or "/var/run/slurmd.pid").replace("%n", node)
    atf.make_bash_script(
        path,
        f"""[ -e {complete_flag} ] || exit 0
sudo pkill -F {pidfile}
sleep 2
sudo {slurmd} -N {node} -b
""",
    )
    return path


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node, complete_flag, slurm_user

    slurm_user = atf.properties["slurm-user"]
    complete_flag = f"{atf.module_tmp_path}/complete_reboot"
    # reboot_script() needs the node name
    node = atf.require_nodes(1)[0]
    atf.require_config_parameter(
        "RebootProgram", reboot_script(f"{atf.module_tmp_path}/reboot.sh")
    )
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    # From 26.05 on, issue a requested reboot and time out an issued one
    # within a second
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("power_save_interval", 1)
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def reset_node():
    yield
    atf.run_command(f"rm -f {complete_flag}", quiet=True, fatal=True)
    atf.cancel_all_jobs(quiet=True, fatal=True)
    # Let an issued reboot complete or time out before resuming the node, so
    # that its RebootProgram is not still running
    atf.wait_for_node_state(
        node, "REBOOT_ISSUED", reverse=True, timeout=timeout_wait, fatal=True
    )
    atf.run_command(f"scontrol cancel_reboot {node}", user=slurm_user, quiet=True)
    atf.run_command(
        f"scontrol update nodename={node} state=resume", user=slurm_user, quiet=True
    )
    # Resuming a DOWN node leaves it NOT_RESPONDING until it registers again,
    # which can take longer than the wait below; a fresh slurmd registers now
    atf.start_slurmd(node, quiet=True)
    atf.wait_for_node_state(node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)


def run_holder():
    """Keep the node busy so that a requested reboot is not issued.

    Wait for the script to run, not only for RUNNING: the launch RPC to slurmd
    completes after the job is RUNNING, and a test that stops slurmd needs it
    done.
    """
    atf.run_command("rm -f holder_started", quiet=True)
    atf.make_bash_script("holder.sh", "touch holder_started\nsleep infinity\n")
    job_id = atf.submit_job_sbatch(f"-N1 -w {node} --time=10 holder.sh", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_file("holder_started", fatal=True)


def drain(reason):
    atf.run_command(
        f"scontrol update nodename={node} state=drain reason={reason}",
        user=slurm_user,
        fatal=True,
    )
    atf.wait_for_node_state(node, "DRAIN", fatal=True)


def reboot(options=""):
    atf.run_command(f"scontrol reboot {options} {node}", user=slurm_user, fatal=True)


def accounting_configured():
    return (
        atf.get_config_parameter("AccountingStorageType", quiet=True)
        == "accounting_storage/slurmdbd"
    )


def cancel_reboot():
    atf.run_command(f"scontrol cancel_reboot {node}", user=slurm_user, fatal=True)
    atf.wait_for_node_state(node, "REBOOT_REQUESTED", reverse=True, fatal=True)


@pytest.mark.xfail(
    atf.get_version() < (26, 11),
    reason="Ticket 25233: cancel_reboot reads a NO_VAL next_state as UNDRAIN",
)
def test_cancel_reboot_keeps_admin_drain():
    """cancel_reboot of a plain reboot leaves an earlier drain and its reason."""

    run_holder()
    drain("admin_drain")
    reboot()
    atf.wait_for_node_state(node, "REBOOT_REQUESTED", fatal=True)

    cancel_reboot()

    state = atf.get_node_parameter(node, "state")
    assert "DRAIN" in state, f"Node should stay drained, got {state}"
    reason = atf.get_node_parameter(node, "reason")
    assert reason == "admin_drain", f"Reason should be kept, got {reason!r}"


def test_cancel_reboot_asap_undrains():
    """cancel_reboot of a reboot ASAP undoes its drain and its reason."""

    run_holder()
    reboot("asap")
    atf.wait_for_node_state(node, "DRAIN", fatal=True)
    reason = atf.get_node_parameter(node, "reason")
    assert reason == "Reboot ASAP", f"ASAP should set the reason, got {reason!r}"

    cancel_reboot()

    state = atf.get_node_parameter(node, "state")
    assert "DRAIN" not in state, f"Node should be undrained, got {state}"
    reason = atf.get_node_parameter(node, "reason")
    assert not reason, f"Reason should be cleared, got {reason!r}"


@pytest.mark.xfail(
    atf.get_version() < (26, 11),
    reason="Ticket 25233: cancel_reboot reads a NO_VAL next_state as UNDRAIN",
)
def test_cancel_reboot_asap_keeps_admin_drain():
    """cancel_reboot of a reboot ASAP on a drained node leaves the drain."""

    run_holder()
    drain("admin_drain")
    reboot("asap")
    atf.wait_for_node_state(node, "REBOOT_REQUESTED", fatal=True)
    reason = atf.get_node_parameter(node, "reason")
    assert reason == "admin_drain", f"ASAP should keep the reason, got {reason!r}"

    cancel_reboot()

    state = atf.get_node_parameter(node, "state")
    assert "DRAIN" in state, f"Node should stay drained, got {state}"
    reason = atf.get_node_parameter(node, "reason")
    assert reason == "admin_drain", f"Reason should be kept, got {reason!r}"


def test_reboot_nextstate_resume_undrains_after_reboot():
    """A drained node returns to service after a nextstate=resume reboot."""

    drain("admin_drain")
    atf.run_command(f"touch {complete_flag}", fatal=True)
    reboot("nextstate=resume")

    # The idle node has its reboot issued at once, RebootProgram restarts
    # slurmd as rebooted, and the node registers, undrains and returns to
    # service with its reason cleared. Allow for the slurmd restart.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, timeout=timeout_wait
    ), "Node should return to IDLE after the nextstate=resume reboot"
    reason = atf.get_node_parameter(node, "reason")
    # Before 26.05, registration clears the reason only when an accounting
    # storage plugin is configured (ac3305964d)
    if atf.get_version() >= (26, 5) or accounting_configured():
        assert not reason, f"Reason should be cleared, got {reason!r}"
    else:
        assert reason.startswith(
            "admin_drain"
        ), f"Reason should be kept without accounting, got {reason!r}"


def test_reboot_times_out_marks_node_down():
    """An issued reboot that never completes downs the node after ResumeTimeout."""

    drain("admin_drain")
    reboot("nextstate=resume")
    atf.wait_for_node_state(node, "REBOOT_ISSUED", fatal=True)
    reason = atf.get_node_parameter(node, "reason")
    assert (
        reason.startswith("admin_drain") and "reboot issued" in reason
    ), f"Expected the admin reason with 'reboot issued' appended, got {reason!r}"

    assert atf.wait_for_node_state(
        node, "REBOOT_ISSUED", reverse=True, timeout=timeout_wait
    ), "The issued reboot should time out"

    state = atf.get_node_parameter(node, "state")
    assert (
        "DOWN" in state and "DRAIN" in state
    ), f"Node should be DOWN and keep its drain, got {state}"
    reason = atf.get_node_parameter(node, "reason")
    assert (
        reason.startswith("admin_drain") and "reboot timed out" in reason
    ), f"Expected the admin reason with 'reboot timed out' appended, got {reason!r}"
    assert "NextState" not in atf.run_command_output(
        f"scontrol show node {node}", fatal=True
    ), "A timed out reboot should leave no NextState"


def test_cancel_reboot_clears_nextstate():
    """cancel_reboot of a nextstate=resume reboot ends the resuming state."""

    run_holder()
    reboot("nextstate=resume")
    atf.wait_for_node_state(node, "REBOOT_REQUESTED", fatal=True)
    assert "NextState=RESUME" in atf.run_command_output(
        f"scontrol show node {node}", fatal=True
    ), "A pending nextstate=resume reboot should show NextState=RESUME"

    cancel_reboot()

    assert "NextState" not in atf.run_command_output(
        f"scontrol show node {node}", fatal=True
    ), "A canceled reboot should leave no NextState"


@pytest.mark.xfail(
    atf.get_version() < (26, 11),
    reason="Ticket 25233: an unexpected reboot reads a NO_VAL next_state as UNDRAIN",
)
def test_unexpected_reboot_keeps_admin_drain():
    """A node that reboots on its own under a pending plain reboot stays drained.

    The controller cancels the pending request and downs the node. The reason
    becomes "Node unexpectedly rebooted", so only the DRAIN flag is checked.
    """

    run_holder()
    drain("admin_drain")
    reboot()
    atf.wait_for_node_state(node, "REBOOT_REQUESTED", fatal=True)

    # Restart slurmd as rebooted: a new boot time while a job runs on the
    # node is an unexpected reboot to the controller, but only if the boot
    # time is later than the controller's last response from the node, in
    # whole seconds. The agent records a node's responses up to 2 seconds
    # late, so start the new slurmd 3 seconds after the old one is gone.
    atf.stop_slurmd(node)
    stopped = int(time.time())
    for _ in atf.timer(timeout=5, poll_interval=0.1, fatal=True, quiet=True):
        if int(time.time()) >= stopped + 3:
            break
    atf.start_slurmd(node, "-b")
    atf.wait_for_node_state(node, "REBOOT_REQUESTED", reverse=True, fatal=True)

    state = atf.get_node_parameter(node, "state")
    assert "DRAIN" in state, f"Node should stay drained, got {state}"
    assert "DOWN" in state, f"An unexpectedly rebooted node should be DOWN, got {state}"
