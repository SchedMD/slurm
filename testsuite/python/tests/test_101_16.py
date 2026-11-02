############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct --kv reports the job's KV, and sacctmgr modify job sets it."""

import glob
import json
import pathlib
import shlex
import time

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    # The archive test purges and reloads job records
    atf.require_accounting(modify=True)
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    atf.require_config_parameter_includes("AccountingStoreFlags", "job_kv")
    # The purge tests share a document between a job and a reservation
    atf.require_config_parameter_includes("AccountingStoreFlags", "resv_kv")
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_version(
        (26, 11), "bin/sacct", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_version(
        (26, 11), "bin/sacctmgr", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_slurm_running()


# How far back the archive test moves the jobs' end time. sacctmgr purges with
# hour granularity and truncates the cutoff to the top of the current hour, so
# a job that just finished is never in range and cannot be archived as-is.
ARCHIVE_BACKDATE_HOURS = 3


def update(job_id, tag, value):
    atf.run_command(
        f"scontrol update jobid={job_id} {tag}={shlex.quote(value)}", fatal=True
    )


# Returned by sacct_kv() when sacct has no row for the job yet
NO_ROW = "no sacct row"


def sacct_kv(job_id):
    """Return the job's KV as sacct --kv reports it, None for NONE."""
    # Only the owner, a coordinator or an admin may read KV, and the test
    # user may have no association, so read it as an admin
    output = atf.run_command_output(
        f"sacct -j {job_id} --kv", user=atf.properties["slurm-user"], fatal=True
    )
    rows = output.splitlines()
    if not rows:
        return NO_ROW
    # A header naming the job, a rule, then the document
    assert len(rows) == 3, f"sacct should report one job {job_id}: {output!r}"
    assert (
        rows[0] == f"KV for {job_id}"
    ), f"sacct should report job {job_id}: {output!r}"
    return None if rows[2] == "NONE" else json.loads(rows[2])


def wait_for_kv(job_id, expected):
    """Wait for sacct to report the expected KV, and return the last one seen."""
    for _ in atf.timer():
        kv = sacct_kv(job_id)
        if kv == expected:
            break
    return kv


def test_sacct_kv():
    """Verify sacct reports KV as it is set, changed and cleared."""

    job_id = atf.submit_job_sbatch('--hold --wrap "sleep infinity"', fatal=True)

    # The quotes and backslash check the document is escaped on its way
    # into the database
    update(job_id, "KV", '{"a":"x\\"y\'z\\\\w"}')
    expected = {"a": "x\"y'z\\w"}
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV set on the job"

    update(job_id, "KVPatch", '[{"op":"add","path":"/b","value":2}]')
    expected = {"a": "x\"y'z\\w", "b": 2}
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV again after it changes"

    update(job_id, "KV", "")
    assert (
        wait_for_kv(job_id, None) is None
    ), "sacct should report a cleared KV as blank"


def test_sacct_kv_from_submit():
    """Verify sacct reports KV given at submission, before the job runs."""

    expected = {"a": "submit"}
    job_id = atf.submit_job_sbatch(
        f"--hold --kv={shlex.quote(json.dumps(expected))} " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV the job was submitted with"


def test_sacct_kv_after_completion():
    """Verify sacct keeps the latest KV of a running job once it ends."""

    job_id = atf.submit_job_sbatch(
        f"--kv={shlex.quote(json.dumps({'a': 'start'}))} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    update(job_id, "KVPatch", '[{"op":"add","path":"/b","value":2}]')
    expected = {"a": "start", "b": 2}
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV changed while the job runs"

    atf.cancel_jobs([job_id], fatal=True)
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    assert (
        sacct_kv(job_id) == expected
    ), "sacct should still report the latest KV after the job ends"


def test_sacct_kv_json():
    """Verify sacct --json --kv gives KV as a nested object, empty without one."""

    expected = {"a": "json", "b": [1, 2]}
    kv_job = atf.submit_job_sbatch(
        f"--hold --kv={shlex.quote(json.dumps(expected))} " '--wrap "sleep infinity"',
        fatal=True,
    )
    plain_job = atf.submit_job_sbatch('--hold --wrap "sleep infinity"', fatal=True)
    assert (
        wait_for_kv(kv_job, expected) == expected
    ), "sacct should report the KV of the job submitted with one"
    assert (
        wait_for_kv(plain_job, None) is None
    ), "sacct should report a blank KV for the job submitted without one"

    for job_id, kv in ((kv_job, expected), (plain_job, {})):
        jobs = json.loads(
            atf.run_command_output(
                f"sacct -j {job_id} -X --json --kv",
                user=atf.properties["slurm-user"],
                fatal=True,
            )
        )["jobs"]
        assert len(jobs) == 1, f"sacct --json should report one job: {jobs}"
        assert (
            jobs[0]["kv"] == kv
        ), f"sacct --json should give job {job_id} the KV object {kv}"

    jobs = json.loads(
        atf.run_command_output(f"sacct -j {kv_job} -X --json", fatal=True)
    )["jobs"]
    assert jobs[0]["kv"] == {}, "sacct --json should only fetch KV with --kv"


@pytest.fixture(scope="function")
def archive_dir(request):
    """A slurmdbd-writable archive directory of the test's own."""
    path = pathlib.Path(f"archive_{request.node.name}").resolve()
    path.mkdir(exist_ok=True)
    # slurmdbd writes the archive file itself, as SlurmUser.
    path.chmod(0o777)
    return path


def run_sql(statement):
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


def kv_hash_inx(job_id):
    """Return the kv_table row the job's record refers to."""
    cluster = atf.get_config_parameter("ClusterName")
    return run_sql(
        f"SELECT kv_hash_inx FROM `{cluster}_job_table` WHERE id_job = {job_id}"
    ).strip()


def kv_rows(hash_inx):
    """Return how many kv_table rows have the given index."""
    cluster = atf.get_config_parameter("ClusterName")
    return run_sql(
        f"SELECT COUNT(*) FROM `{cluster}_kv_table` WHERE hash_inx = {hash_inx}"
    ).strip()


def test_kv_survives_archive(archive_dir):
    """Verify job KV survives an archive dump and load, and none stays blank.

    A document another job still refers to must not be purged.
    """

    # The quotes and backslash check the document is escaped on its way
    # back into the database
    expected = {"a": "x\"y'z\\w", "test": "archive"}
    shared = {"test": "archive shared"}
    kv_job = atf.submit_job_sbatch(
        f"--kv={shlex.quote(json.dumps(expected))} --wrap true", fatal=True
    )
    plain_job = atf.submit_job_sbatch("--wrap true", fatal=True)
    old_job, new_job = (
        atf.submit_job_sbatch(
            f"--kv={shlex.quote(json.dumps(shared))} --wrap true", fatal=True
        )
        for _ in range(2)
    )
    for job_id in (kv_job, plain_job, old_job, new_job):
        atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    assert (
        wait_for_kv(kv_job, expected) == expected
    ), "sacct should report the KV of the ended job before archiving"
    assert (
        wait_for_kv(plain_job, None) is None
    ), "sacct should report a blank KV for the ended job without one"
    for job_id in (old_job, new_job):
        assert (
            wait_for_kv(job_id, shared) == shared
        ), f"sacct should report the shared KV of job {job_id}"
    kv_inx = kv_hash_inx(kv_job)
    shared_inx = kv_hash_inx(new_job)

    # Move the jobs' end time out of the current hour. The purge cutoff is the
    # top of the current hour minus the requested hours, so a job that just
    # ended is never in range. The purge is cluster-wide, but every other job
    # in this module ended within the hour or has not ended. new_job stays in
    # the current hour, so the document it shares with old_job stays in use.
    cluster = atf.get_config_parameter("ClusterName")
    end_time = int(time.time()) - ARCHIVE_BACKDATE_HOURS * 3600
    run_sql(
        f"UPDATE `{cluster}_job_table` SET time_end = {end_time} "
        f"WHERE id_job IN ({kv_job}, {plain_job}, {old_job})"
    )

    atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Jobs "
        "PurgeJobAfter=1hours",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for job_id in (kv_job, plain_job, old_job):
        assert (
            sacct_kv(job_id) == NO_ROW
        ), f"The archive dump should have purged job {job_id}"
    assert (
        kv_rows(kv_inx) == "0"
    ), "The archive dump should have purged the document no job refers to"
    assert (
        kv_rows(shared_inx) == "1"
    ), "The archive dump should keep the document a remaining job refers to"
    assert (
        sacct_kv(new_job) == shared
    ), "The remaining job should still report the shared KV"

    # slurmdbd names the file <cluster>_<table>_archive_<start>_<end>. The
    # leading underscore keeps out the job_env and job_script archives.
    archive_files = []
    for table in ("job_table", "kv_table"):
        files = glob.glob(f"{archive_dir}/*_{table}_archive_*")
        assert (
            len(files) == 1
        ), f"Expected exactly one {table} archive file in {archive_dir}; got {files}"
        archive_files += files

    for archive_file in archive_files:
        atf.run_command(
            f"sacctmgr -i archive load File={archive_file}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    assert (
        wait_for_kv(kv_job, expected) == expected
    ), "The reloaded job should report the KV it was archived with"
    assert (
        wait_for_kv(plain_job, None) is None
    ), "The reloaded job without KV should still report a blank KV"
    assert (
        wait_for_kv(old_job, shared) == shared
    ), "The reloaded job should report the KV it shares with a remaining job"


def test_kv_purged_when_unused(archive_dir):
    """Verify a purge removes a replaced KV document, with no job to purge."""

    old = {"test": "replaced", "n": 1}
    new = {"test": "replaced", "n": 2}
    job_id = atf.submit_job_sbatch(
        f"--hold --kv={shlex.quote(json.dumps(old))} " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert (
        wait_for_kv(job_id, old) == old
    ), "sacct should report the KV the job was submitted with"
    old_inx = kv_hash_inx(job_id)

    update(job_id, "KV", json.dumps(new))
    assert wait_for_kv(job_id, new) == new, "sacct should report the replaced KV"
    new_inx = kv_hash_inx(job_id)
    assert kv_rows(old_inx) == "1", "The replaced document should stay until a purge"

    # No job here ended that long ago, so only the kv table has anything to
    # purge
    atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Jobs "
        "PurgeJobAfter=12months",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        kv_rows(old_inx) == "0"
    ), "The purge should remove the document no job refers to"
    assert (
        kv_rows(new_inx) == "1"
    ), "The purge should keep the document the job refers to"
    assert sacct_kv(job_id) == new, "The job should still report its KV"
    archive_files = glob.glob(f"{archive_dir}/*_kv_table_archive_*")
    assert (
        len(archive_files) == 1
    ), f"Expected the removed document archived in {archive_dir}; got {archive_files}"

    # A job going back to the purged document stores it again
    update(job_id, "KV", json.dumps(old))
    assert (
        wait_for_kv(job_id, old) == old
    ), "A job should report a document again after the purge removed it"


def sacctmgr_modify_kv(job_id, value):
    return atf.run_command(
        f"sacctmgr -i modify job where jobid={job_id} set KV={shlex.quote(value)}"
    )


def test_sacctmgr_modify_job_kv():
    """Verify sacctmgr modify job sets and clears KV, only once the job ends."""

    job_id = atf.submit_job_sbatch("--wrap 'sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        wait_for_kv(job_id, None) is None
    ), "sacct should report a blank KV for the job submitted without one"

    result = sacctmgr_modify_kv(job_id, '{"a":"running"}')
    assert (
        result["exit_code"] != 0
    ), f"sacctmgr should refuse to set the KV of a running job: {result}"
    assert (
        sacct_kv(job_id) is None
    ), "A refused sacctmgr modify should leave the job's KV alone"

    atf.cancel_jobs([job_id], fatal=True)
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    result = sacctmgr_modify_kv(job_id, '{"a":"ended"}')
    assert (
        result["exit_code"] == 0
    ), f"sacctmgr should set the KV of an ended job: {result}"
    expected = {"a": "ended"}
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV sacctmgr set on the ended job"

    result = sacctmgr_modify_kv(job_id, "")
    assert (
        result["exit_code"] == 0
    ), f"sacctmgr should clear the KV of an ended job: {result}"
    assert (
        wait_for_kv(job_id, None) is None
    ), "sacct should report a blank KV once sacctmgr clears it"


def test_sacctmgr_modify_job_kv_not_owner():
    """Verify sacctmgr refuses to set the KV of another user's ended job."""

    job_id = atf.submit_job_sbatch(
        "--wrap true", user=atf.properties["slurm-user"], fatal=True
    )
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    result = sacctmgr_modify_kv(job_id, '{"a":1}')
    assert (
        result["exit_code"] != 0
    ), f"sacctmgr should refuse to set the KV of another user's job: {result}"
    assert (
        sacct_kv(job_id) is None
    ), "A refused sacctmgr modify should leave the job's KV alone"


@pytest.mark.parametrize("value", ["[1,2]", "not json"])
def test_sacctmgr_modify_job_kv_bad_value(value):
    """Verify sacctmgr refuses KV that is not a JSON object."""

    expected = {"a": 1}
    job_id = atf.submit_job_sbatch(
        f"--kv={shlex.quote(json.dumps(expected))} --wrap true", fatal=True
    )
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV the job was submitted with"

    result = sacctmgr_modify_kv(job_id, value)
    assert result["exit_code"] != 0, f"sacctmgr should refuse KV={value}: {result}"
    assert (
        sacct_kv(job_id) == expected
    ), "A refused sacctmgr modify should leave the job's KV alone"


def test_sacctmgr_modify_job_kv_pending():
    """Verify sacctmgr refuses to set the KV of a pending job."""

    expected = {"a": 1}
    job_id = atf.submit_job_sbatch(
        f"--hold --kv={shlex.quote(json.dumps(expected))} " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV the job was submitted with"

    result = sacctmgr_modify_kv(job_id, '{"b":1}')
    assert (
        result["exit_code"] != 0
    ), f"sacctmgr should refuse to set the KV of a pending job: {result}"
    assert (
        sacct_kv(job_id) == expected
    ), "A refused sacctmgr modify should leave the job's KV alone"


def test_sacctmgr_modify_job_keeps_kv():
    """Verify sacctmgr modify job of another field leaves KV alone."""

    expected = {"a": 1}
    job_id = atf.submit_job_sbatch(
        f"--kv={shlex.quote(json.dumps(expected))} --wrap true", fatal=True
    )
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    assert (
        wait_for_kv(job_id, expected) == expected
    ), "sacct should report the KV the job was submitted with"

    atf.run_command(
        f"sacctmgr -i modify job where jobid={job_id} set DerivedExitCode=1",
        fatal=True,
    )
    assert (
        sacct_kv(job_id) == expected
    ), "Modifying another field should leave the job's KV alone"


def test_kv_stored_once():
    """Verify jobs with the same KV document share one stored copy."""

    expected = {"shared": True}
    kv = shlex.quote(json.dumps(expected))
    job_ids = [
        atf.submit_job_sbatch(f"--kv={kv} --wrap true", fatal=True) for _ in range(2)
    ]
    array_id = atf.submit_job_sbatch(f"--array=0-2 --kv={kv} --wrap true", fatal=True)
    for job_id in job_ids + [f"{array_id}_{i}" for i in range(3)]:
        atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
        assert (
            wait_for_kv(job_id, expected) == expected
        ), f"sacct should report the KV of job {job_id}"

    cluster = atf.get_config_parameter("ClusterName")
    ids = ", ".join(str(job_id) for job_id in job_ids)
    rows = run_sql(
        f"SELECT DISTINCT kv_hash_inx FROM `{cluster}_job_table` "
        f"WHERE id_job IN ({ids}) OR id_array_job = {array_id}"
    ).split()
    assert len(rows) == 1, f"Every job should refer to one stored document: {rows}"


def test_kv_changed_while_slurmdbd_down():
    """Verify a document replaced before slurmctld sent it is stored later.

    slurmdbd asks for a document it lacks, but while it is down the job's
    document can change again, so slurmctld only sends the latest one. A later
    job with the replaced document must still get it stored.
    """

    first, replaced, last = ({"test": "slurmdbd down", "n": n} for n in (1, 2, 3))
    job_id = atf.submit_job_sbatch(
        f"--hold --kv={shlex.quote(json.dumps(first))} " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert (
        wait_for_kv(job_id, first) == first
    ), "sacct should report the KV the job was submitted with"

    atf.stop_slurmdbd(quiet=True)
    try:
        for doc in (replaced, last):
            update(job_id, "KV", json.dumps(doc))
    finally:
        atf.start_slurmdbd(quiet=True)
    assert (
        wait_for_kv(job_id, last) == last
    ), "sacct should report the job's latest KV once slurmdbd is back"

    later_job = atf.submit_job_sbatch(
        f"--hold --kv={shlex.quote(json.dumps(replaced))} " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert (
        wait_for_kv(later_job, replaced) == replaced
    ), "A later job should report the document the first job replaced"


@pytest.fixture
def make_reservation():
    """Create reservations with KV, and delete any left at the end."""

    admin = atf.properties["slurm-user"]
    names = []

    def create(name, start, kv):
        atf.run_command(
            f"scontrol create reservation reservationname={name} "
            f"starttime={start} duration=10 nodecnt=1 users={admin} "
            f"KV={shlex.quote(json.dumps(kv))}",
            user=admin,
            fatal=True,
        )
        names.append(name)

    yield create
    # A test may already have deleted its reservation
    for name in names:
        atf.run_command(f"scontrol delete reservation={name}", user=admin)


def resv_kv_hash_inx(name):
    """Wait for the reservation's record, and return the kv row it refers to."""
    cluster = atf.get_config_parameter("ClusterName")
    for _ in atf.timer(fatal=True):
        rows = run_sql(
            f"SELECT DISTINCT kv_hash_inx FROM `{cluster}_resv_table` "
            f"WHERE resv_name = '{name}'"
        ).split()
        if rows:
            break
    assert len(rows) == 1, f"Reservation {name} should refer to one document"
    return rows[0]


def test_kv_job_purge_keeps_reservation_document(archive_dir, make_reservation):
    """Verify a job purge keeps a document a reservation still refers to."""

    doc = {"test": "job purge, shared with a reservation"}
    make_reservation("resv_kv_job_purge", "now+1hour", doc)
    # A time limit lets the job run before the reservation takes the node
    job_id = atf.submit_job_sbatch(
        f"-t1 --kv={shlex.quote(json.dumps(doc))} --wrap true", fatal=True
    )
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    assert wait_for_kv(job_id, doc) == doc, "sacct should report the job's KV"
    hash_inx = kv_hash_inx(job_id)
    assert (
        resv_kv_hash_inx("resv_kv_job_purge") == hash_inx
    ), "The job and the reservation should share one stored document"

    cluster = atf.get_config_parameter("ClusterName")
    end_time = int(time.time()) - ARCHIVE_BACKDATE_HOURS * 3600
    run_sql(
        f"UPDATE `{cluster}_job_table` SET time_end = {end_time} "
        f"WHERE id_job = {job_id}"
    )
    atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Jobs "
        "PurgeJobAfter=1hours",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert sacct_kv(job_id) == NO_ROW, "The archive dump should have purged the job"
    assert (
        kv_rows(hash_inx) == "1"
    ), "The purge should keep the document the reservation refers to"


def test_kv_resv_purge_keeps_job_document(archive_dir, make_reservation):
    """Verify a reservation purge keeps a document a job still refers to."""

    admin = atf.properties["slurm-user"]
    doc = {"test": "reservation purge, shared with a job"}
    job_id = atf.submit_job_sbatch(
        f"--hold --kv={shlex.quote(json.dumps(doc))} " '--wrap "sleep infinity"',
        fatal=True,
    )
    assert wait_for_kv(job_id, doc) == doc, "sacct should report the job's KV"
    hash_inx = kv_hash_inx(job_id)
    name = "resv_kv_resv_purge"
    make_reservation(name, "now", doc)
    assert (
        resv_kv_hash_inx(name) == hash_inx
    ), "The job and the reservation should share one stored document"

    # Only a reservation that has ended can be purged. Deleting one that has
    # started ends it now and marks its record deleted.
    atf.run_command(f"scontrol delete reservation={name}", user=admin, fatal=True)
    cluster = atf.get_config_parameter("ClusterName")
    table = f"`{cluster}_resv_table`"
    where = f"WHERE resv_name = '{name}'"
    for _ in atf.timer(fatal=True):
        if run_sql(f"SELECT deleted FROM {table} {where}").strip() == "1":
            break
    shift = ARCHIVE_BACKDATE_HOURS * 3600
    run_sql(
        f"UPDATE {table} SET time_start = time_start - {shift}, "
        f"time_end = time_end - {shift} {where}"
    )

    atf.run_command(
        f"sacctmgr -i archive dump Directory={archive_dir} Reservations "
        "PurgeResvAfter=1hours",
        user=admin,
        fatal=True,
    )
    assert (
        run_sql(f"SELECT COUNT(*) FROM {table} {where}").strip() == "0"
    ), "The archive dump should have purged the reservation"
    assert (
        kv_rows(hash_inx) == "1"
    ), "The purge should keep the document the job refers to"
    assert sacct_kv(job_id) == doc, "The job should still report its KV"
