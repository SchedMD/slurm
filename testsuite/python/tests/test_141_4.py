############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
# Tests for cloud node behavior with enable_stepmgr active. Kept separate
# from test_141_1 because enabling stepmgr at the module level would alter
# the behavior of every test in that suite.
import subprocess
import time

import pytest

import atf

pytestmark = pytest.mark.slow

suspend_time = 10
suspend_timeout = 10
# Generous ResumeTimeout: srun launches in the background and the test needs a
# few seconds to detect POWERING_UP and register a slurmd, so the node must not
# be marked failed before that manual slurmd comes up.
resume_timeout = 45


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Runs slurmd on same machine as slurmctld")
    atf.require_version(
        (26, 5, 3),
        "sbin/slurmctld",
        reason="Ticket 25564: slurmctld must not set SLURM_STEPMGR before"
        " batch_host is known",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    atf.require_config_parameter_includes("SlurmctldParameters", "idle_on_node_suspend")
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")

    atf.require_config_parameter("NodeName", {"node1": {"State": "CLOUD"}})
    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": "ALL", "Default": "YES"},
        },
    )
    # Don't run the usual atf.require_slurm_running() because the test starts
    # the slurmd manually
    atf.start_slurmctld(clean=True)

    yield

    # conftest only cancels jobs when it started Slurm itself (it keys off
    # properties["slurm-started"], which require_slurm_running() sets and
    # start_slurmctld() does not), so cancel them here before killing the
    # daemons. Otherwise a failure earlier in the test leaves the allocation
    # and the orphaned background srun behind.
    atf.cancel_all_jobs()

    # conftest doesn't stop daemons it didn't start, so stop them here. This
    # also covers the manually started node1 slurmd, which is not one of the
    # slurmds conftest knows about.
    atf.stop_slurmctld(also_slurmds=True)


# Regression test for bug 25564: srun against a POWERED_DOWN node with
# enable_stepmgr active aborted srun with a glibc "free(): invalid pointer"
# because node_info left the alias_addrs output pointer uninitialized. The
# controller advertised SLURM_STEPMGR as the literal string "(null)" for a job
# whose batch_host wasn't picked yet, so srun looked up that bogus stepmgr and
# then freed the uninitialized pointer in slurm_job_step_create().
def test_srun_on_powered_down_node_with_stepmgr():
    assert "POWERED_DOWN" in atf.get_node_parameter(
        "node1", "state"
    ), "Cloud node must start in POWERED_DOWN state to reproduce the bug"

    # Launch srun in the background against the powered-down cloud node. srun
    # requesting node1 triggers a power-up; ResumeProgram=/bin/true does not
    # actually start a slurmd, so we register one manually (as test_141_1 does)
    # to let the job run. The job prints its Slurm node name via
    # $SLURMD_NODENAME.
    output_file = "srun.out"
    # Run srun directly instead of atf.submit_job_srun(background=True): that
    # helper returns only the job id and drops the Popen object, and the
    # symptom of this regression is srun itself aborting, so the test needs
    # srun's exit code and stderr.
    srun_process = atf.run_command(
        f"srun -w node1 --output={output_file} bash -c 'echo $SLURMD_NODENAME'",
        background=True,
    )["process"]

    # srun requesting the node transitions it to POWERING_UP
    powering_up = atf.wait_for_node_state("node1", "POWERING_UP")

    # An srun that dies at launch never powers the node up, so the wait above
    # would just time out and blame power save while srun's own error sat
    # unread. fatal= cannot be combined with background=True, so check the
    # process explicitly and report its output first.
    if srun_process.poll() is not None:
        try:
            stdout, stderr = srun_process.communicate(timeout=atf.PERIODIC_TIMEOUT)
        except subprocess.TimeoutExpired:
            stdout = stderr = "<unavailable, srun did not exit>"
        pytest.fail(
            f"srun exited (rc={srun_process.returncode}) before node1 powered up."
            f" stdout: {stdout}, stderr: {stderr}"
        )
    assert powering_up, "node1 did not reach POWERING_UP after srun requested it"

    # Pin both preconditions of Ticket 25564 while they still hold. The bug
    # needs STEPMGR_ENABLED on the job AND a NULL batch_host, which only
    # happens inside the CONFIGURING window - the node is still powering up
    # here, so batch_host has not been picked. Without these assertions the
    # test would keep passing if enable_stepmgr silently stopped applying to
    # cloud allocations, and would guard nothing.
    atf.repeat_until(
        lambda: len(atf.get_jobs(quiet=True)),
        lambda count: count == 1,
        timeout=15,
        fatal=True,
    )
    job_id = list(atf.get_jobs(quiet=True))[0]
    assert (
        atf.get_job_parameter(job_id, "StepMgrEnabled", default="No", quiet=True)
        == "Yes"
    ), f"Job {job_id} must be stepmgr-enabled to reproduce the bug"
    assert (
        atf.get_job_parameter(job_id, "JobState", quiet=True) == "CONFIGURING"
    ), f"Job {job_id} must still be CONFIGURING (batch_host unpicked)"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register a real slurmd so the cloud node actually resumes
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N node1",
        fatal=True,
        user="root",
    )

    # Node finishes powering up and the job completes. Wait for a positive
    # state rather than "not POWERING_UP": the inverted form is also satisfied
    # by the DOWN+POWERED_DOWN state slurmctld sets at ResumeTimeout, so a
    # slurmd that never registers would be misreported as an srun failure.
    atf.wait_for_node_state(
        "node1",
        ["ALLOCATED", "MIXED", "IDLE"],
        timeout=resume_timeout + 5,
        fatal=True,
    )
    # Cloud node registration/configuration can take up to PERIODIC_TIMEOUT
    # seconds (bug 16459), so give the job ample time to finish.
    try:
        stdout, stderr = srun_process.communicate(timeout=atf.PERIODIC_TIMEOUT + 30)
    except subprocess.TimeoutExpired:
        srun_process.kill()
        # kill() may only reach a sudo wrapper: run_command() runs the command
        # under "sudo ... /bin/bash -lc" whenever SlurmTestUser is set, and sudo
        # forks rather than execs, so srun survives as an orphan still holding
        # the stdout/stderr pipes. Bound this communicate() too - unbounded, it
        # blocks on an EOF that never arrives and pytest hangs with no failure
        # reported and no teardown.
        try:
            stdout, stderr = srun_process.communicate(timeout=atf.PERIODIC_TIMEOUT)
        except subprocess.TimeoutExpired:
            stdout = stderr = "<unavailable, srun did not exit>"
        pytest.fail(
            "srun did not exit after node1 finished powering up."
            f" stdout: {stdout}, stderr: {stderr}"
        )

    # This is the regression check: before the fix srun died here with a glibc
    # "free(): invalid pointer" abort (SIGABRT, so a negative returncode).
    assert (
        "invalid pointer" not in stderr
    ), f"srun aborted freeing an uninitialized pointer. stderr: {stderr}"
    assert srun_process.returncode == 0, (
        f"srun exited with rc={srun_process.returncode}"
        f" (a negative value is the signal that killed it)."
        f" stdout: {stdout}, stderr: {stderr}"
    )

    atf.assert_file_contents(
        output_file,
        "node1",
        message="srun failed to run the task on node1",
    )
