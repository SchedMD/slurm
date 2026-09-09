############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""slurmd keeps waiting for a temporarily-absent slurmctld (Ticket 24793).

slurmstepd retries its completion RPC to slurmctld forever by design
(_send_complete_batch_script_msg) so accounting is never lost. slurmd only stops
waiting for a step's completion when slurmd ITSELF is shutting down (see
test_105_14). When slurmctld is merely temporarily gone -- a crash that will be
restarted, or a `scontrol reconfigure` that re-execs it -- slurmd is NOT shutting
down, so it must keep WAITING; it must not give up (which would lose accounting)
and must not deadlock. This checks:
 - a crashed slurmctld: while it is gone, slurmd (not shutting down) and the
   job's slurmstepd both keep waiting -- neither gives up; and
 - `scontrol reconfigure` with a job still completing is not treated as a
   shutdown, so the job is not abandoned and completes normally.
"""

import pytest

import atf

MESSAGE_TIMEOUT = 5
# KillWait keeps the SIGTERM-ignoring step (and its slurmstepd) alive across the
# moment slurmctld is taken away, so the completion is genuinely still pending.
KILL_WAIT = 10
# Bound for a killed daemon's process to disappear. Use atf's own default
# polling timeout rather than a shorter one, so this does not become a
# spurious failure on a loaded machine.
GONE_BOUND = atf.default_polling_timeout
# Long enough to cover KillWait plus a slurmstepd retry (RETRY_DELAY=15s), to
# confirm the daemons keep waiting rather than giving up.
WAIT_MARGIN = KILL_WAIT + 20
# slurmctld stays up across a reconfigure, so a correctly-handled completion is
# delivered promptly; a gross regression that stalled it would blow this bound.
RECONFIG_DONE_BOUND = 60

pytestmark = [pytest.mark.slow]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 24793: keep waiting while slurmctld is temporarily away",
    )
    atf.require_nodes(1)
    atf.require_config_parameter("MessageTimeout", MESSAGE_TIMEOUT)
    atf.require_config_parameter("KillWait", KILL_WAIT)
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def restore_cluster():
    """The crash test leaves a SIGTERM-ignoring slurmstepd (and its wedge task)
    alive; SIGKILL them and rebuild a clean cluster after each test."""
    yield
    slurmstepd_exe = f"{atf.properties['slurm-sbin-dir']}/slurmstepd"
    pids = atf.pids_from_exe(slurmstepd_exe)
    if pids:
        # "|| true" already makes this command always succeed, so fatal=True
        # would never fire; a failed kill (e.g. an unkillable stepd) must not
        # be silently swallowed here, or it shows up later as a confusing
        # failure in atf.start_slurm() instead.
        atf.run_command(
            "kill -9 " + " ".join(str(p) for p in pids) + " 2>/dev/null || true",
            user="root",
        )
    # The '[w]edge' regex matches the wedge task but not pkill's own command
    # line, so pkill does not SIGKILL the shell running it.
    atf.run_command("pkill -9 -f '[w]edge_job.sh' 2>/dev/null || true", user="root")
    atf.start_slurm(clean=True, quiet=True)


def _kill9(exe):
    pids = atf.pids_from_exe(exe)
    if pids:
        pid_list = " ".join(str(p) for p in pids)
        atf.run_command(f"kill -9 {pid_list} 2>/dev/null || true", user="root")


def _wait_gone(exe, timeout):
    for _ in atf.timer(timeout=timeout):
        if not atf.pids_from_exe(exe):
            return True
    return False


def _submit_completing_job(node):
    """Start a batch job and cancel it so it is COMPLETING, with slurmstepd held
    alive (its batch-complete RPC still pending) by a SIGTERM-ignoring script."""
    atf.make_bash_script("wedge_job.sh", "trap '' TERM\nwhile true; do sleep 1; done\n")
    job_id = atf.submit_job_sbatch(f"-w {node} wedge_job.sh", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(f"scancel {job_id}", user=atf.properties["slurm-user"], fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETING", fatal=True)
    return job_id


def test_slurmd_keeps_waiting_for_crashed_slurmctld(restore_cluster):
    """While slurmctld is crashed and slurmd is NOT shutting down, slurmd and the
    job's slurmstepd both keep waiting for the completion to become deliverable --
    neither gives up (which would lose accounting)."""
    node = next(iter(atf.nodes))
    slurmctld_exe = f"{atf.properties['slurm-sbin-dir']}/slurmctld"
    slurmd_exe = f"{atf.properties['slurm-sbin-dir']}/slurmd"
    slurmstepd_exe = f"{atf.properties['slurm-sbin-dir']}/slurmstepd"

    _submit_completing_job(node)
    assert atf.pids_from_exe(
        slurmstepd_exe
    ), "a slurmstepd should be running the completing job"

    # Crash slurmctld but leave slurmd running: slurmd is NOT shutting down, so it
    # must keep waiting for the (now undeliverable) completion, not give up.
    _kill9(slurmctld_exe)
    assert _wait_gone(slurmctld_exe, GONE_BOUND), "slurmctld should be gone"

    # After a full retry cycle both must still be waiting. State cannot be queried
    # with slurmctld down, so assert on the processes.
    atf.run_command(f"sleep {WAIT_MARGIN}", quiet=True, fatal=True)
    assert atf.pids_from_exe(
        slurmd_exe
    ), "slurmd must keep waiting for slurmctld, not exit"
    assert atf.pids_from_exe(
        slurmstepd_exe
    ), "slurmstepd must keep retrying its completion, not give up"


def test_reconfigure_does_not_abandon_completion(restore_cluster):
    """`scontrol reconfigure` is not a slurmd shutdown, so a still-completing job
    is not abandoned. With slurmctld up throughout, the job completes promptly and
    normally across the reconfigure (a gross stall would blow the bound)."""
    node = next(iter(atf.nodes))

    job_id = _submit_completing_job(node)

    atf.run_command(
        "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
    )

    atf.wait_for_job_state(job_id, "DONE", timeout=RECONFIG_DONE_BOUND, fatal=True)
