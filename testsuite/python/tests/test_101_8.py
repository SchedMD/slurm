############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a job step's own time limit is captured and reported by sacct.

Ticket 21130 makes an explicitly requested step time limit part of the
accounting record: slurmctld ships it in the step start RPC, slurmdbd stores
it in <cluster>_step_table.timelimit, sacct reports it on the step row through
Timelimit/TimelimitRaw, and data_parser exposes it as
.jobs[].steps[].time.limit.  Before this only the job row carried a time
limit, so a step that had asked for a shorter one left no trace in
accounting.

Every step here is launched from a batch script on purpose: srun only requests
a step time limit when it runs inside an allocation it did not create itself.
When srun allocates for itself, _set_step_opts() moves -t onto the job and
clears it for the step.

What ends up stored, and how sacct renders it:

    srun -t N     N          Timelimit "00:0N:00"   TimelimitRaw "N"
    srun          INFINITE   Timelimit ""           TimelimitRaw ""
    batch/extern  INFINITE   Timelimit ""           TimelimitRaw ""

A step with no limit renders as an empty field, not as the "UNLIMITED" that a
job row with no limit shows.
"""

import glob
import json
import pathlib
import shlex
import time

import pytest

import atf

# Allocation limit.  Larger than every step limit below, so the job never ends
# before the step limit under test would have mattered.
JOB_TIME_LIMIT = 10

# What a step asks for with -t, and what scontrol update raises it to.
STEP_TIME_LIMIT = 5
UPDATED_STEP_TIME_LIMIT = 7

# How far back the archive test moves a step's end time.  sacctmgr purges with
# hour granularity and truncates the cutoff to the top of the current hour, so
# a step that just finished is never in range and cannot be archived as-is.
ARCHIVE_BACKDATE_HOURS = 3


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


def _time_str(minutes):
    """Render minutes the way sacct's mins2time_str() does."""
    days, remainder = divmod(minutes, 1440)
    hours, mins = divmod(remainder, 60)
    if days:
        return f"{days}-{hours:02d}:{mins:02d}:00"
    return f"{hours:02d}:{mins:02d}:00"


def _sacct_rows(job_id):
    """Return {JobID: (Timelimit, TimelimitRaw)} for every row of the job.

    --parsable2 leaves the fields unpadded, so a step with no time limit is
    recognizable as a genuinely empty field rather than as blank padding.  -j
    alone defaults the query window to Epoch 0 through now, which keeps the
    archive test's backdated step inside it.
    """
    output = atf.run_command_output(
        f"sacct -j {job_id} --parsable2 --noheader " "-o JobID,Timelimit,TimelimitRaw",
        fatal=True,
        quiet=True,
    )

    rows = {}
    for line in output.splitlines():
        if not line.strip():
            continue
        job_id_field, timelimit, timelimit_raw = line.split("|")
        rows[job_id_field] = (timelimit, timelimit_raw)
    return rows


def _sacct_step_end(job_id, step_id):
    """Return the End time sacct reports for a step."""
    return atf.run_command_output(
        f"sacct -j {job_id}.{step_id} --parsable2 --noheader -o End",
        fatal=True,
        quiet=True,
    ).strip()


def _submit_job_with_steps(script_name, body):
    """Submit a batch job running the given script body and return its job id."""
    script = script_name
    atf.make_bash_script(script, body)
    return atf.submit_job_sbatch(
        f"-N1 -t {JOB_TIME_LIMIT} -o /dev/null {script}", fatal=True
    )


def test_sacct_reports_step_timelimit():
    """A step's -t is recorded; a step without one keeps an empty field.

    The job row must keep reporting the allocation's limit, so that the step
    limit is reported in addition to it rather than in place of it.
    """
    job_id = _submit_job_with_steps(
        "step_timelimit.sh",
        f"srun -t {STEP_TIME_LIMIT} true\nsrun true\n",
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    # Step 1 is the last step to be created, so its record implies step 0's.
    atf.wait_for_step_accounted(job_id, 1, fatal=True)

    rows = _sacct_rows(job_id)

    assert rows.get(f"{job_id}.0") == (
        _time_str(STEP_TIME_LIMIT),
        str(STEP_TIME_LIMIT),
    ), (
        f"Ticket 21130: sacct should report the step's own time limit for "
        f"{job_id}.0; got {rows}"
    )

    assert f"{job_id}.1" in rows, f"sacct never reported step {job_id}.1; got {rows}"

    # Every other row -- the step that asked for nothing, plus batch and (when
    # PrologFlags=Contain is set) extern -- must stay blank.  A step with no
    # explicit -t is stored as INFINITE, which renders empty.
    for step_id, values in rows.items():
        if step_id in (str(job_id), f"{job_id}.0"):
            continue
        assert values == ("", ""), (
            f"Ticket 21130: step {step_id} never requested a time limit, so "
            f"sacct should leave Timelimit and TimelimitRaw empty; got {values}"
        )

    assert rows.get(str(job_id)) == (
        _time_str(JOB_TIME_LIMIT),
        str(JOB_TIME_LIMIT),
    ), (
        f"the job row for {job_id} should still report the allocation's time "
        f"limit; got {rows}"
    )


def test_sacct_json_reports_step_timelimit():
    """data_parser exposes the step time limit as .jobs[].steps[].time.limit."""
    job_id = _submit_job_with_steps(
        "step_timelimit_json.sh",
        f"srun -t {STEP_TIME_LIMIT} true\nsrun true\n",
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.wait_for_step_accounted(job_id, 1, fatal=True)

    output = atf.run_command_output(f"sacct -j {job_id} --json", fatal=True, quiet=True)
    jobs = json.loads(output)["jobs"]
    assert (
        len(jobs) == 1
    ), f"sacct --json should report exactly one job for {job_id}; got {len(jobs)}"

    limits = {step["step"]["id"]: step["time"]["limit"] for step in jobs[0]["steps"]}

    limited = limits.get(f"{job_id}.0")
    assert limited == {
        "set": True,
        "infinite": False,
        "number": STEP_TIME_LIMIT,
    }, (
        f"Ticket 21130: .jobs[].steps[].time.limit should carry the requested "
        f"{STEP_TIME_LIMIT} minutes for {job_id}.0; got {limited}"
    )

    # A step that asked for nothing is stored as INFINITE, which UINT32_NO_VAL
    # dumps with infinite true and set false.
    unlimited = limits.get(f"{job_id}.1")
    assert unlimited == {"set": False, "infinite": True, "number": 0}, (
        f"Ticket 21130: {job_id}.1 requested no time limit, so time.limit "
        f"should be infinite; got {unlimited}"
    )


def test_scontrol_update_step_timelimit_recorded():
    """scontrol update of a running step's TimeLimit reaches the database.

    _update_step() calls jobacct_storage_g_step_start() so the new limit
    overwrites the stored one; without that, sacct would keep reporting the
    limit the step was created with.
    """
    job_id = _submit_job_with_steps(
        "step_timelimit_update.sh",
        f"srun -t {STEP_TIME_LIMIT} sleep infinity\n",
    )
    # Only a JOB_RUNNING step is updated; a step that merely exists is not.
    atf.wait_for_step(job_id, 0, fatal=True)
    atf.wait_for_step_accounted(job_id, 0, fatal=True)

    rows = _sacct_rows(job_id)
    assert rows.get(f"{job_id}.0") == (
        _time_str(STEP_TIME_LIMIT),
        str(STEP_TIME_LIMIT),
    ), f"sacct should report the step's requested time limit first; got {rows}"

    atf.run_command(
        f"scontrol update StepId={job_id}.0 TimeLimit={UPDATED_STEP_TIME_LIMIT}",
        fatal=True,
    )

    for _ in atf.timer(fatal=True):
        limit = atf.get_step_parameter(f"{job_id}.0", "TimeLimit", quiet=True)
        if limit == _time_str(UPDATED_STEP_TIME_LIMIT):
            break

    for _ in atf.timer():
        rows = _sacct_rows(job_id)
        if rows.get(f"{job_id}.0") == (
            _time_str(UPDATED_STEP_TIME_LIMIT),
            str(UPDATED_STEP_TIME_LIMIT),
        ):
            break
    else:
        assert False, (
            f"Ticket 21130: the updated time limit should reach accounting for "
            f"step {job_id}.0; sacct still reports {rows}"
        )


@pytest.fixture(scope="function")
def archive_dir():
    """A slurmdbd-writable archive directory, for auto-config only.

    The dump below purges rows out of the live step table, and only
    auto-config restores the database backup that setup() takes.  Demanding
    auto-config explicitly keeps this test from purging a database it cannot
    put back.
    """
    atf.require_auto_config("purges and reloads step accounting records")

    path = pathlib.Path("archive").resolve()
    path.mkdir(exist_ok=True)
    # slurmdbd writes the archive file itself, as SlurmUser.
    path.chmod(0o777)
    return path


def _run_sql(statement):
    """Run one statement against the slurmdbd database.

    ATF has no SQL helper, so the connection parameters are read out of
    slurmdbd.conf the same way atf.dump_accounting_database() does.
    """
    conf = atf.get_config(live=False, source="slurmdbd", quiet=True)

    options = []
    if conf.get("StorageHost"):
        options.append(f"-h {conf['StorageHost']}")
    if conf.get("StoragePort"):
        options.append(f"-P {conf['StoragePort']}")
    options.append(f"-u {conf.get('StorageUser') or atf.properties['slurm-user']}")
    if conf.get("StoragePass"):
        options.append(f"-p{conf['StoragePass']}")
    database = conf.get("StorageLoc") or "slurm_acct_db"

    return atf.run_command_output(
        f"mysql {' '.join(options)} {database} -N -B -e {shlex.quote(statement)}",
        fatal=True,
        quiet=True,
    )


def test_step_timelimit_survives_archive(archive_dir):
    """A step's time limit survives an archive dump and reload.

    _pack_local_step()/_unpack_local_step() carry the timelimit column, and
    _load_steps() puts it back; without that the reloaded step would come back
    with no time limit at all.
    """
    job_id = _submit_job_with_steps(
        "step_timelimit_archive.sh",
        f"srun -t {STEP_TIME_LIMIT} true\n",
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.wait_for_step_accounted(job_id, 0, fatal=True)

    expected = (_time_str(STEP_TIME_LIMIT), str(STEP_TIME_LIMIT))
    rows = _sacct_rows(job_id)
    assert (
        rows.get(f"{job_id}.0") == expected
    ), f"sacct should report the step's time limit before archiving; got {rows}"

    # time_end is written by the step-complete record, which lands after the
    # row itself exists; backdating before it arrives would be overwritten.
    for _ in atf.timer(fatal=True):
        if _sacct_step_end(job_id, 0) not in ("", "Unknown"):
            break

    # Move this step's end time out of the current hour.  The purge cutoff is
    # the top of the current hour minus the requested hours, so a step that just
    # ended is never in range.  The purge itself is cluster-wide; leaving the
    # batch step alone is what keeps the assertions below meaningful.
    cluster = atf.get_config_parameter("ClusterName")
    end_time = int(time.time()) - ARCHIVE_BACKDATE_HOURS * 3600
    _run_sql(
        f"UPDATE `{cluster}_step_table` s "
        f"JOIN `{cluster}_job_table` j ON s.job_db_inx = j.job_db_inx "
        f"SET s.time_end = {end_time} "
        f"WHERE j.id_job = {job_id} AND s.id_step = 0"
    )

    atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Steps "
        "PurgeStepAfter=1hours",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    rows = _sacct_rows(job_id)
    assert f"{job_id}.0" not in rows, (
        f"the archive dump should have purged step {job_id}.0 from the step "
        f"table; got {rows}"
    )
    assert (
        f"{job_id}.batch" in rows
    ), f"the batch step was not in range and should have survived; got {rows}"

    # slurmdbd names the file <cluster>_<table>_archive_<start>_<end>.
    archive_files = glob.glob(f"{archive_dir}/*step_table_archive*")
    assert (
        len(archive_files) == 1
    ), f"expected exactly one step archive file in {archive_dir}; got {archive_files}"

    atf.run_command(
        f"sacctmgr -i archive load File={archive_files[0]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    for _ in atf.timer():
        rows = _sacct_rows(job_id)
        if rows.get(f"{job_id}.0") == expected:
            break
    else:
        assert False, (
            f"Ticket 21130: the reloaded step {job_id}.0 should keep its time "
            f"limit {expected}; sacct reports {rows}"
        )
