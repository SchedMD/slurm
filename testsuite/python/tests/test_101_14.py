############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A job's exit status and its Slurm return code are reported separately.

  ExitCode  the wait(2) status of the job's own processes, shown as
            <status>:<signal>
  SlurmRC   the Slurm error code when Slurm itself failed the job, and 0
            otherwise

The cases below are where the two could be confused:

  exit 200   200 is above 128, so it could be mistaken for a signal. It
             must stay in the status half with SlurmRC=0.
  bad chdir  Slurm fails the job before the script runs. SlurmRC must hold
             the error code and ExitCode must be 1:0.

Jobs are submitted with --no-requeue so a site that has RequeueExit
configured cannot requeue them out from under the assertions.
"""

import glob
import json
import pathlib
import shlex
import time

import pytest

import atf

# Every Slurm error code is 1000 or greater (the lowest anchor in
# slurm/slurm_errno.h), and every wait(2) exit status is below 256. So a
# SlurmRC of 1000 or more is proof the value came from the error-code space
# rather than from a job's exit status leaking across.
LOWEST_SLURM_ERROR_CODE = 1000

# A working directory the stepd cannot open a job's output file in, which is
# how these tests make Slurm fail a job before its script ever runs. Two
# properties are load-bearing:
#   - The job must use the DEFAULT output file. expand_stdio_fields() joins
#     this directory onto slurm-%j.out when the step record is built, so the
#     stepd opens an absolute path under it. An explicit --output would
#     disarm the primitive and the job would simply run.
#   - The directory must be one the stepd cannot CREATE, not merely one that
#     is absent: on ENOENT it calls mkdirpath() and retries the open, so a
#     missing directory under a writable parent is created and the job runs.
#     Nesting it under /dev/null gives ENOTDIR instead, on every node and
#     whichever user the job runs as.
UNUSABLE_WORK_DIR = "/dev/null/slurm_test_workdir"

# How far back the archive test moves a job's end time. sacctmgr purges with
# hour granularity and truncates the cutoff to the top of the current hour, so
# a job that just finished is never in range and cannot be archived as-is.
ARCHIVE_BACKDATE_HOURS = 3


@pytest.fixture(scope="module", autouse=True)
def setup():
    # The archive test purges and reloads job records.
    atf.require_accounting(modify=True)
    # The job and step rows read below do not exist under nojobs or nosteps.
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nosteps")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    atf.require_slurm_running()


def _sacct_job_row(job_id):
    """Return (State, ExitCode, SlurmRC) as accounting recorded them for the job."""
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    output = atf.run_command_output(
        f"sacct -j {job_id} -X --noheader -P -o State,ExitCode,SlurmRC", fatal=True
    ).strip()
    fields = output.split("|")
    assert len(fields) == 3, f"Unexpected sacct output for job {job_id}: {output!r}"
    return fields[0], fields[1], fields[2]


requires_rc = pytest.mark.skipif(
    atf.get_version("bin/sacct") < (26, 11),
    reason="Ticket 24195: the SlurmRC field was added in 26.11",
)


@requires_rc
@pytest.mark.parametrize(
    "wrap, expected_state, expected_exit_code",
    [
        ("exit 0", "COMPLETED", "0:0"),
        ("exit 3", "FAILED", "3:0"),
        ("exit 200", "FAILED", "200:0"),
    ],
)
def test_job_exit_status_leaves_rc_zero(wrap, expected_state, expected_exit_code):
    """Verify a job's own exit status is never reported as a Slurm return code"""

    job_id = atf.submit_job_sbatch(f"--no-requeue --wrap '{wrap}'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    state, exit_code, rc = _sacct_job_row(job_id)

    assert state == expected_state, f"Job running '{wrap}' should be {expected_state}"
    assert exit_code == expected_exit_code, (
        f"Job running '{wrap}' should report ExitCode {expected_exit_code}, got "
        f"{exit_code}"
    )
    assert rc == "0", (
        f"Job running '{wrap}' ended on its own, so no Slurm error code applies "
        f"and SlurmRC must be 0; got {rc}. A non-zero SlurmRC here means the "
        "exit status leaked into the return code."
    )


@requires_rc
def test_slurm_failure_sets_rc_and_reports_exit_code_1_0():
    """Verify a job Slurm failed reports its error code in SlurmRC, not in ExitCode"""

    # An unusable working directory makes the stepd fail while opening the
    # job's output file, before the script ever runs.
    job_id = atf.submit_job_sbatch(
        f"--no-requeue --chdir={UNUSABLE_WORK_DIR} --wrap 'exit 0'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    state, exit_code, rc = _sacct_job_row(job_id)

    assert (
        state == "FAILED"
    ), f"A job Slurm could not launch should be FAILED, got {state}"
    assert exit_code == "1:0", (
        f"A job Slurm failed should report ExitCode 1:0, got {exit_code}. A "
        "value of 0:1 means the exit status is being read as a signal, and a "
        "four-digit status means the Slurm error code leaked into ExitCode."
    )
    assert rc.isdigit() and int(rc) >= LOWEST_SLURM_ERROR_CODE, (
        f"A job Slurm failed should report the error code in SlurmRC, got {rc}. "
        f"Every Slurm error code is >= {LOWEST_SLURM_ERROR_CODE}, so a smaller "
        "value means an exit status was stored in SlurmRC."
    )


@requires_rc
def test_rc_is_a_distinct_field_in_json():
    """Verify sacct --json reports slurm_rc alongside, and separate from, exit_code"""

    job_id = atf.submit_job_sbatch(
        f"--no-requeue --chdir={UNUSABLE_WORK_DIR} --wrap 'exit 0'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    _, _, rc = _sacct_job_row(job_id)
    assert (
        rc.isdigit() and int(rc) >= LOWEST_SLURM_ERROR_CODE
    ), f"A job Slurm failed should report the error code in SlurmRC, got {rc}"

    output = atf.run_command_output(f"sacct --json -j {job_id}", fatal=True)
    jobs = json.loads(output)["jobs"]
    assert len(jobs) == 1, f"Expected one job record for {job_id}, got {len(jobs)}"
    job = jobs[0]

    assert "slurm_rc" in job, "sacct --json should report slurm_rc on the job object"
    assert int(job["slurm_rc"]) == int(rc), (
        f"json slurm_rc should match the SlurmRC sacct reports, {rc}; got "
        f"{job['slurm_rc']}"
    )

    # exit_code is a structured object describing the process status. slurm_rc is a
    # plain number. Reading one as the other is the conflation that SlurmRC exists
    # to prevent, so assert they disagree rather than merely that both exist.
    assert "exit_code" in job, "sacct --json should still report exit_code"
    assert (
        job["exit_code"].get("return_code", {}).get("number") == 1
    ), f"json exit_code should describe an exit status of 1, got {job['exit_code']}"


@requires_rc
def test_rc_appears_in_long_output():
    """Verify SlurmRC is included in sacct --long"""

    job_id = atf.submit_job_sbatch("--no-requeue --wrap 'exit 0'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    # -P keeps the header as full field names rather than width-truncated
    # column labels, so the match cannot be fooled by formatting.
    output = atf.run_command_output(f"sacct -j {job_id} -X --long -P", fatal=True)
    header = output.splitlines()[0]
    assert "SlurmRC" in header.split("|"), (
        f"sacct --long should include the SlurmRC column (ticket 24195); header was "
        f"{header}"
    )


@requires_rc
def test_steps_have_no_return_code():
    """Verify SlurmRC is reported for jobs only, never for a step

    job_exit_code.shtml states that return codes are recorded for jobs only,
    and that a step has an exit code and a signal but no return code. SlurmRC is a
    valid sacct format field on any row, so a user will ask for it on a step
    and needs a blank rather than a misleading 0.
    """

    job_id = atf.submit_job_sbatch("--no-requeue --wrap 'exit 0'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    # wait_for_job_accounted polls `sacct -X`, so it waits on the job row
    # while the assertions below read step rows. If a step row were ever
    # slower to land than the job row, this would fail by finding no step
    # rows at all rather than by reading a wrong value, which the assertion
    # below reports explicitly.
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    rows = atf.run_command_output(
        f"sacct -j {job_id} --noheader -P -o JobID,SlurmRC", fatal=True
    ).strip()

    job_rc, step_rcs = None, []
    for row in rows.splitlines():
        row_id, _, rc = row.partition("|")
        if "." in row_id:
            step_rcs.append((row_id, rc))
        else:
            job_rc = rc

    assert job_rc == "0", f"The job row should report SlurmRC 0, got {job_rc!r}"
    assert step_rcs, f"Expected at least one step row for job {job_id}: {rows!r}"
    for row_id, rc in step_rcs:
        assert rc == "", (
            f"Step {row_id} should report no return code, got {rc!r}. A value "
            "here would mean steps had started carrying one."
        )


@pytest.fixture(scope="function")
def archive_dir():
    """A slurmdbd-writable archive directory."""
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


@requires_rc
def test_rc_survives_archive(archive_dir):
    """Verify a job's SlurmRC survives an archive dump and load

    _pack_local_job()/_unpack_local_job() carry the slurm_rc column, and
    _load_jobs() puts it back; without that the reloaded job would come back
    with SlurmRC 0.
    """

    job_id = atf.submit_job_sbatch(
        f"--no-requeue --chdir={UNUSABLE_WORK_DIR} --wrap 'exit 0'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    before = _sacct_job_row(job_id)
    rc = before[2]
    assert rc.isdigit() and int(rc) >= LOWEST_SLURM_ERROR_CODE, (
        f"A job Slurm failed should report its error code in SlurmRC before "
        f"archiving; got {before}"
    )

    # Move this job's end time out of the current hour. The purge cutoff is
    # the top of the current hour minus the requested hours, so a job that
    # just ended is never in range. The purge is cluster-wide, but every
    # other job in this module ended within the hour.
    cluster = atf.get_config_parameter("ClusterName")
    end_time = int(time.time()) - ARCHIVE_BACKDATE_HOURS * 3600
    _run_sql(
        f"UPDATE `{cluster}_job_table` SET time_end = {end_time} "
        f"WHERE id_job = {job_id}"
    )

    atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Jobs "
        "PurgeJobAfter=1hours",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    output = atf.run_command_output(
        f"sacct -j {job_id} -X --noheader -P -o JobID", fatal=True
    )
    assert (
        str(job_id) not in output.split()
    ), f"The archive dump should have purged job {job_id}; sacct still shows it"

    # slurmdbd names the file <cluster>_<table>_archive_<start>_<end>. The
    # leading underscore keeps out the job_env and job_script archives.
    archive_files = glob.glob(f"{archive_dir}/*_job_table_archive_*")
    assert (
        len(archive_files) == 1
    ), f"Expected exactly one job archive file in {archive_dir}; got {archive_files}"

    atf.run_command(
        f"sacctmgr -i archive load File={archive_files[0]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    after = _sacct_job_row(job_id)
    assert after == before, (
        f"The reloaded job {job_id} should report the same State, ExitCode "
        f"and SlurmRC as before archiving, {before}; got {after}. SlurmRC 0 here means "
        "slurm_rc was lost in the archive."
    )
