############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""slurmd can shut down while a job is still completing, without losing it (Ticket 24793).

A slurmstepd retries its completion RPC to slurmctld forever by design, so
accounting is never lost. Before the fix, shutting slurmd down while a job was
still completing made slurmd's terminate handler wait forever for that stepd;
slurmd's shutdown drain (_wait_for_all_threads) never returned, so slurmd could
not be stopped.

The fix lets slurmd, once it has itself been asked to shut down, stop waiting
for the stepd and return from the terminate handler WITHOUT running the epilog
or sending epilog_complete -- leaving the job COMPLETING for slurmctld to
re-drive on restart. slurmctld's own shutdown stays immediate (it never waits on
job state), and the slurmstepd survives slurmd (with its still-pending
completion), so nothing is discarded. This checks that slurmd stops and that the
stepd (and its completion) is preserved, not killed. The companion test_105_13
covers the temporary-absence (crash / reconfigure) case, where slurmd is NOT
shutting down and so must keep waiting.
"""

import pytest

import atf

MESSAGE_TIMEOUT = 5
# KillWait keeps the SIGTERM-ignoring step (and its slurmstepd) genuinely
# running -- so the job is really still completing -- when the shutdown begins.
KILL_WAIT = 15
# slurmctld must exit well within this even with a stuck completion: its
# shutdown must stay independent of job state (a controller that waited for
# completing jobs would hang forever, never finite). At least the atf default
# polling timeout so this does not become a spurious failure under load.
CTLD_SHUTDOWN_BOUND = atf.default_polling_timeout
# Finite upper bound on slurmd shutting down. The pre-fix bug was an INFINITE
# hang (the drain is _wait_for_all_threads(MAX(prolog_timeout, epilog_timeout)),
# and both default to NO_VAL16 = wait-forever unless PrologEpilogTimeout is set --
# which this test intentionally leaves unset), so any finite bound catches the
# regression. It is generous because, while slurmctld is unreachable, conmgr is
# slow to process slurmd's shutdown, so the fix's abandon can take tens of
# seconds to fire.
SHUTDOWN_BOUND = 150
# Bound for a killed daemon's process to disappear. Use atf's own default
# polling timeout rather than a shorter one, so this does not become a
# spurious failure on a loaded machine.
GONE_BOUND = atf.default_polling_timeout

pytestmark = [pytest.mark.slow]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 24793: slurmd shutdown while a job is completing",
    )
    atf.require_nodes(1)
    atf.require_config_parameter("MessageTimeout", MESSAGE_TIMEOUT)
    atf.require_config_parameter("KillWait", KILL_WAIT)
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def restore_cluster():
    """These tests deliberately leave an orphaned, SIGTERM-ignoring slurmstepd
    (holding a pending completion) alive; SIGKILL it and its wedge task so the
    clean restart starts from a truly idle node."""
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


def _signal(exe, sig):
    pids = atf.pids_from_exe(exe)
    if pids:
        pid_list = " ".join(str(p) for p in pids)
        atf.run_command(f"kill {sig} {pid_list} 2>/dev/null || true", user="root")


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


def test_shutdown_lets_slurmd_stop(restore_cluster):
    """`scontrol shutdown` with a still-completing job lets both slurmctld and
    slurmd exit -- slurmctld promptly (it never waits on the job) and slurmd
    within a finite bound (the pre-fix bug was an infinite hang)."""
    node = next(iter(atf.nodes))
    slurmctld_exe = f"{atf.properties['slurm-sbin-dir']}/slurmctld"
    slurmd_exe = f"{atf.properties['slurm-sbin-dir']}/slurmd"
    slurmstepd_exe = f"{atf.properties['slurm-sbin-dir']}/slurmstepd"

    job_id = _submit_completing_job(node)

    # The job must still genuinely be completing (its slurmstepd still up)
    # right before the shutdown, or this test proves nothing: if the step
    # had already finished, slurmd would exit promptly with or without the
    # fix, and the assertions below would pass vacuously.
    assert atf.pids_from_exe(
        slurmstepd_exe
    ), "a slurmstepd should still be running the completing job"
    assert (
        atf.get_job_parameter(job_id, "JobState") == "COMPLETING"
    ), "job should still be completing right before the shutdown"

    # Graceful full-cluster shutdown while the job is still completing.
    atf.run_command("scontrol shutdown", user=atf.properties["slurm-user"], fatal=True)

    # slurmctld must exit promptly -- coupling its shutdown to job state would
    # hang it forever (never finite).
    assert _wait_gone(slurmctld_exe, CTLD_SHUTDOWN_BOUND), (
        "slurmctld did not shut down promptly with a job still completing; its "
        "shutdown must stay independent of job state (Ticket 24793)"
    )

    # slurmd must finish shutting down. Before the fix it waited forever for the
    # still-completing job's stepd and hung here.
    assert _wait_gone(slurmd_exe, SHUTDOWN_BOUND), (
        f"slurmd did not shut down within {SHUTDOWN_BOUND}s with a job still "
        f"completing (Ticket 24793)"
    )


def test_shutdown_preserves_the_stepd(restore_cluster):
    """When slurmd is stopped while a completion cannot be delivered (slurmctld
    already gone), slurmd abandons the wait and exits, but must NOT take the
    slurmstepd (or its pending completion) down with it -- the completion is
    preserved for delivery once the cluster is back."""
    node = next(iter(atf.nodes))
    slurmctld_exe = f"{atf.properties['slurm-sbin-dir']}/slurmctld"
    slurmd_exe = f"{atf.properties['slurm-sbin-dir']}/slurmd"
    slurmstepd_exe = f"{atf.properties['slurm-sbin-dir']}/slurmstepd"

    _submit_completing_job(node)
    assert atf.pids_from_exe(
        slurmstepd_exe
    ), "a slurmstepd should be running the completing job"

    # Take slurmctld away first, so the stepd's completion is genuinely
    # undeliverable, then ask slurmd to stop. This isolates the abandon path
    # (with slurmctld gone, pre-fix slurmd hangs forever here).
    _signal(slurmctld_exe, "-9")
    assert _wait_gone(slurmctld_exe, GONE_BOUND), "slurmctld should be gone"
    _signal(slurmd_exe, "-TERM")

    # slurmd abandons the undeliverable completion and exits ...
    assert _wait_gone(slurmd_exe, SHUTDOWN_BOUND), (
        f"slurmd did not shut down within {SHUTDOWN_BOUND}s while a completion "
        f"was pending (Ticket 24793)"
    )
    # ... but the slurmstepd survives, still holding the pending completion.
    assert atf.pids_from_exe(slurmstepd_exe), (
        "slurmstepd should survive slurmd shutdown so it can still deliver the "
        "job's completion (Ticket 24793)"
    )
