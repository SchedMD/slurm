############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobcomp/lua job state transition events."""

import re

import pytest

import atf

jobcomp_log = None

# The event name is the second argument of slurm_jobcomp_log_record(). The
# job fields come from the job record table. The job name is logged to find
# the records of a job whose id was never reported, and state_desc goes last
# because it may contain spaces.
lua_script_template = """
function slurm_jobcomp_log_record(job, event)
    local f = io.open("{log}", "a")
    f:write(string.format(
        "job_id=%d name=%s event=%s job_state=%d priority=%d " ..
        "restart_cnt=%d sluid=%s state_reason=%s state_desc=%s\\n",
        job.job_id, job.name, event, job.job_state, job.priority,
        job.restart_cnt, job.sluid, tostring(job.state_reason),
        tostring(job.state_desc)))
    f:close()
    return slurm.SUCCESS
end
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    global jobcomp_log

    # require_config_file() can only verify .conf and .yaml files on a
    # local-config site, so the jobcomp.lua install needs auto-config
    atf.require_auto_config("wants to install a jobcomp.lua script")
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51114: jobcomp/lua job state transition events require 26.11+",
    )
    # jobcomp/lua is only built with lua support
    atf.require_build_config("HAVE_LUA")

    # slurmctld writes the log from its own working directory, so it needs
    # an absolute path
    jobcomp_log = atf.module_tmp_path / "jobcomp.log"

    # The plugin loads jobcomp.lua from the directory of slurm.conf
    atf.require_config_file("jobcomp.lua", lua_script_template.format(log=jobcomp_log))

    atf.require_config_parameter("JobCompType", "jobcomp/lua")
    # enable_job_start is left out on purpose: the start event must not reach
    # the script unless enabled
    for event in ("queue", "hold", "release", "requeue"):
        atf.require_config_parameter_includes("JobCompParams", f"enable_job_{event}")
    # A requeued job is only eligible again after requeue_delay, which defaults
    # to the AuthInfo cred_expire (120 seconds), and is only started by the
    # next scheduling cycle. The (key, value) form replaces a value the site
    # config already sets.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter_includes("ScronParameters", "enable")
    atf.require_slurm_running()


def get_records(line_start):
    """Return the records the script logged whose line matches, as dicts."""
    # The script creates the log on its first call, so it may not exist yet
    output = atf.run_command_output(f"cat {jobcomp_log}", quiet=True)
    records = []
    for line in re.findall(rf"^{line_start} .*$", output, flags=re.MULTILINE):
        records.append(dict(re.findall(r"(\w+)=(.*?)(?= \w+=|$)", line)))
    return records


def get_job_records(job_id):
    """Return the records the script logged for a job, as dicts."""
    return get_records(f"job_id={job_id}")


def wait_for_records(job_id, expected_events):
    """Return the job's records once it has one per expected event.

    Fails if the records do not show up in time or if their event names do
    not match expected_events in order.
    """
    records = []
    for t in atf.timer():
        records = get_job_records(job_id)
        if len(records) >= len(expected_events):
            break

    events = [record["event"] for record in records]
    assert (
        events == expected_events
    ), f"Job {job_id} produced the events {events}, expected {expected_events}"
    return records


def test_submit_held_then_release():
    """A job submitted held is queued, released, and finished.

    The hold reason of a job submitted with --hold is set when the job is
    created, not through a hold transition, so the queue record reports it
    held and no hold record follows. The job then starts, but enable_job_start
    is not set, so the start event must not reach the script.
    """
    job_id = atf.submit_job_sbatch("--hold -t1 -o /dev/null --wrap true", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "Reason", fatal=True) == "JobHeldUser"
    ), f"Job {job_id} submitted with --hold should be pending as JobHeldUser"
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    queue, release, finish = wait_for_records(
        job_id, ["job queue", "job release", "job finish"]
    )
    assert (
        queue["state_reason"] == "JobHeldUser"
    ), f"Queue record of a held job: {queue}"
    assert int(queue["priority"]) == 0, f"Queue record of a held job: {queue}"
    assert int(release["priority"]) > 0, f"Release record: {release}"
    assert (
        release["state_reason"] == "None"
    ), f"Release record should carry no reason: {release}"
    assert (
        release["state_desc"] == "nil"
    ), f"An unset state_desc should reach the script as nil: {release}"


def test_hold_and_release_pending_job():
    """Holding a pending job and releasing it produce one record each.

    Repeating the hold or the release does not change the job, so neither
    adds a record.
    """
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    atf.run_command(f"scontrol hold {job_id}", fatal=True)
    # Only the records matter here, not the exit status of the repetition
    atf.run_command(f"scontrol hold {job_id}")
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.run_command(f"scontrol release {job_id}")
    atf.cancel_jobs([job_id], fatal=True)

    queue, hold, release, finish = wait_for_records(
        job_id, ["job queue", "job hold", "job release", "job finish"]
    )
    assert hold["state_reason"] in (
        "JobHeldUser",
        "JobHeldAdmin",
    ), f"Hold record reason: {hold['state_reason']}"
    assert int(hold["priority"]) == 0, f"Hold record: {hold}"
    assert int(release["priority"]) > 0, f"Release record: {release}"


def test_requeue_hold_running_job():
    """Requeuing a running job held ends its run and queues a new one held.

    The run that ended is reported by a finish event whose job_state carries
    the JOB_REQUEUE flag, then the requeue event carries the new SLUID and
    run number of the job and the hold the requeue applied, with its reason
    and description; no hold event precedes it. Releasing the job is a
    normal release, and the finish event of the second run shares the SLUID
    and run number of the requeue event.
    """
    job_id = atf.submit_job_sbatch(
        "--requeue -t5 -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(f"scontrol requeuehold {job_id}", fatal=True)
    # The hold is reported by the requeue event, which only exists once the
    # requeue has completed, so wait for it before releasing the job.
    wait_for_records(job_id, ["job queue", "job finish", "job requeue"])
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    queue, finish1, requeue, release, finish2 = wait_for_records(
        job_id,
        ["job queue", "job finish", "job requeue", "job release", "job finish"],
    )
    # JOB_REQUEUE, the flag a requeue leaves on the run it ended; lua has no
    # constant for it
    job_requeue = 1 << 10
    assert (
        int(finish1["job_state"]) & job_requeue
    ), f"The requeued run should finish with JOB_REQUEUE set: {finish1['job_state']}"
    assert (
        requeue["sluid"] != finish1["sluid"]
    ), f"Requeue record should carry a new SLUID: {requeue['sluid']}"
    assert (
        int(requeue["restart_cnt"]) == int(finish1["restart_cnt"]) + 1
    ), f"Requeue record should carry the next run number: {requeue['restart_cnt']}"
    assert int(requeue["priority"]) == 0, f"Requeue record of a held job: {requeue}"
    assert (
        requeue["state_reason"] == "JobHeldUser"
    ), f"Requeue record reason: {requeue['state_reason']}"
    assert (
        requeue["state_desc"] == "job requeued in held state"
    ), f"Requeue record should describe the hold: {requeue}"
    assert int(release["priority"]) > 0, f"Release record: {release}"
    assert (
        release["state_desc"] == "nil"
    ), f"An unset state_desc should reach the script as nil: {release}"
    assert (
        finish2["sluid"] == requeue["sluid"]
    ), f"Second run record should carry the SLUID of the requeue record: {finish2}"
    assert (
        finish2["restart_cnt"] == requeue["restart_cnt"]
    ), f"Second run should keep the requeue run number: {finish2['restart_cnt']}"
    assert not (
        int(finish2["job_state"]) & job_requeue
    ), f"The cancelled run should not finish as requeued: {finish2['job_state']}"


def test_crontab_job():
    """A scrontab job is queued at install, and removing its crontab ends it.

    scrontab submits a job for each crontab entry when the crontab is
    installed, so the job enters the queue like any other submission and
    its queue event must come then, not only once the job finishes.
    Removing the crontab cancels the job.
    """
    name = "cron_172_7"
    # 00:00 on January 1st, so the job stays pending for the whole test
    atf.run_command("scrontab -r", quiet=True)
    atf.run_command(
        "scrontab -", input=f"#SCRON --job-name={name}\n0 0 1 1 * true\n", fatal=True
    )
    try:
        job_ids = []
        for t in atf.timer():
            job_ids = atf.run_command_output(
                f"squeue --me --noheader --format=%A --name={name}", quiet=True
            ).split()
            if job_ids:
                break
        assert len(job_ids) == 1, f"Expected one cron job named {name}: {job_ids}"
        job_id = int(job_ids[0])
        (queue,) = wait_for_records(job_id, ["job queue"])
    finally:
        atf.run_command("scrontab -r", quiet=True)

    wait_for_records(job_id, ["job queue", "job finish"])
    assert (
        queue["state_reason"] == "BeginTime"
    ), f"Queue record of a cron job waiting for its time: {queue}"


def test_crontab_rejected_entry():
    """A crontab rejected for one entry ends the jobs of its other entries.

    scrontab submits the jobs of a crontab one by one and drops those already
    submitted when a later entry is rejected. They had entered the queue, so
    they must leave it with a finish event rather than vanish after their
    queue event. The rejected crontab reports no job id, so the records are
    found by the job name.
    """
    name = "cron_rejected_172_7"
    atf.run_command("scrontab -r", quiet=True)
    atf.run_command(
        "scrontab -",
        input=(
            f"#SCRON --job-name={name}\n0 0 1 1 * true\n"
            "#SCRON --partition=no_such_partition_172_7\n0 0 1 1 * true\n"
        ),
        xfail=True,
        fatal=True,
    )

    records = []
    for t in atf.timer():
        records = get_records(rf"job_id=\d+ name={name}")
        if len(records) >= 2:
            break
    events = [record["event"] for record in records]
    assert events == [
        "job queue",
        "job finish",
    ], f"The dropped cron job {name} produced the events {events}"
    # JOB_CANCELLED is the base state, in the low byte of job_state
    assert (
        int(records[1]["job_state"]) & 0xFF == 4
    ), f"The dropped cron job should finish cancelled: {records[1]}"
