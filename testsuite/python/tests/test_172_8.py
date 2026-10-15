############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the jobcomp/lua job start event."""

import re

import pytest

import atf

jobcomp_log = None

# The event name is the second argument of slurm_jobcomp_log_record(). The
# job fields come from the job record table. state_desc goes last because
# it may contain spaces.
lua_script_template = """
function slurm_jobcomp_log_record(job, event)
    local f = io.open("{log}", "a")
    f:write(string.format(
        "job_id=%d event=%s priority=%d restart_cnt=%d sluid=%s " ..
        "state_reason=%s state_desc=%s\\n",
        job.job_id, event, job.priority, job.restart_cnt, job.sluid,
        tostring(job.state_reason), tostring(job.state_desc)))
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
    # Only the start event is enabled, so a job must record exactly its start
    # and its finish
    atf.require_config_parameter_includes("JobCompParams", "enable_job_start")
    # A requeued job is only eligible again after requeue_delay, which defaults
    # to the AuthInfo cred_expire (120 seconds), and is only started by the
    # next scheduling cycle. The (key, value) form replaces a value the site
    # config already sets.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


def get_job_records(job_id):
    """Return the records the script logged for a job, as dicts."""
    # The script creates the log on its first call, so it may not exist yet
    output = atf.run_command_output(f"cat {jobcomp_log}", quiet=True)
    records = []
    for line in re.findall(rf"^job_id={job_id} .*$", output, flags=re.MULTILINE):
        records.append(dict(re.findall(r"(\w+)=(.*?)(?= \w+=|$)", line)))
    return records


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


def test_start_and_finish():
    """A job records its start when enable_job_start is set.

    The job start event reaches the script with the "job start" name, after
    which only the finish follows: the job is held and released before it
    runs, but queue, hold, release, and requeue stay off since they are not
    enabled.
    """
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    atf.run_command(f"scontrol hold {job_id}", fatal=True)
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.run_command(f"scontrol update JobId={job_id} StartTime=now", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    start, finish = wait_for_records(job_id, ["job start", "job finish"])
    assert int(start["priority"]) > 0, f"Start record: {start}"
    assert start["sluid"] == finish["sluid"], f"Records of one run: {start} {finish}"


def test_start_on_each_run():
    """A requeued job records a start for each of its runs.

    The run the requeue ended is reported by a finish event, then the second
    run is recorded as a start of its own, carrying the new SLUID and run
    number the requeue gave the job, so a start event stands for one run and
    not for the job as a whole.
    """
    job_id = atf.submit_job_sbatch(
        "--requeue -t5 -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    start1, finish1, start2, finish2 = wait_for_records(
        job_id, ["job start", "job finish", "job start", "job finish"]
    )
    assert (
        start2["sluid"] != start1["sluid"]
    ), f"Second run should start with a new SLUID: {start2['sluid']}"
    assert (
        start2["sluid"] == finish2["sluid"]
    ), f"Records of the second run: {start2} {finish2}"
    assert (
        int(start2["restart_cnt"]) == int(start1["restart_cnt"]) + 1
    ), f"Second run should carry the next run number: {start2['restart_cnt']}"
