############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct's SLUID reporting across job resizes and requeues."""

import json
import logging
import re
import shlex
from datetime import datetime

import pytest

import atf

# Progress messages the resize job script writes to its output file.
MSG_READY = "Ready to get signaled"
MSG_RESIZED = "Resize done"
MSG_FAILED = "Resize scaffolding failed"

# Pinned so an inherited SLURM_TIME_FORMAT can't change what sacct prints.
SACCT_TIME_FORMAT = "%Y-%m-%dT%H:%M:%S"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5), "bin/sacct", reason="Ticket 22180: SLUID availability added in 26.05+"
    )

    atf.require_accounting()
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


def get_and_assert_sluid(job_id, quiet=False):
    """Get the SLUID for a given job_id via scontrol -d show job.

    Note this is the SLUID the command tools report, which survives a resize
    and only changes on a requeue. The per-record SLUID that changes on a
    resize too is only visible through sacct.
    """

    sluid = atf.get_job_parameter(job_id, "SLUID", quiet=quiet)
    assert sluid is not None, f"Job {job_id} has no SLUID"
    return sluid


def dbd_agent_queue_size():
    """Return slurmctld's count of messages still queued for slurmdbd."""

    output = atf.run_command_output("sdiag", quiet=True, fatal=True)
    match = re.search(r"DBD Agent queue size:\s*(\d+)", output)
    if match is None:
        pytest.fail(f"sdiag reported no DBD agent queue size:\n{output}")
    return int(match.group(1))


def wait_for_dbd_agent_drained(job_id):
    """Wait until slurmctld has nothing left queued for slurmdbd.

    A duplicate key leaves the message queued and retried, so the queue
    draining is what says the accounting records all landed. Two readings are
    required so a queue that is only momentarily empty doesn't pass.
    """

    drained_polls = 0
    for t in atf.timer():
        drained_polls = drained_polls + 1 if dbd_agent_queue_size() == 0 else 0
        if drained_polls == 2:
            return
    pytest.fail(f"slurmdbd agent queue never drained for job {job_id}")


def wait_for_new_sluid(job_id, previous_sluid):
    """Wait for a requeued job to get its new SLUID and return it.

    The requeued job only stays PENDING for about a scheduling cycle, so poll
    for the SLUID itself instead of trying to catch that state.
    """

    for t in atf.timer():
        sluid = get_and_assert_sluid(job_id, quiet=True)
        if sluid != previous_sluid:
            return sluid
    pytest.fail(f"Job {job_id} still has SLUID {previous_sluid} after the requeue")


def parse_sacct_time(value):
    """Convert a sacct timestamp into a datetime."""

    try:
        return datetime.strptime(value, SACCT_TIME_FORMAT)
    except ValueError:
        pytest.fail(f"Unparsable sacct timestamp {value!r}")


def parse_sacct_records(output, fields):
    """Split sacct -P output into a list of dicts, one per record."""

    records = []
    for line in output.splitlines():
        if not line.strip():
            continue
        values = line.split("|")
        if len(values) != len(fields):
            pytest.fail(f"Expected {len(fields)} fields in sacct line {line!r}")
        records.append(dict(zip(fields, values)))
    return records


def wait_for_sacct_records(job_id, fields, expected_count, fatal=True):
    """Wait until sacct -D reports exactly expected_count records.

    Returns the records in the order sacct listed them, oldest first. With
    `fatal` unset the last records seen are returned instead of failing, so
    the caller can assert the count itself.
    """

    output = ""
    records = []
    for t in atf.timer():
        output = atf.run_command_output(
            f"sacct -j {job_id} -D -X -P --noheader -o {','.join(fields)}",
            env_vars=f"SLURM_TIME_FORMAT={SACCT_TIME_FORMAT}",
            fatal=True,
        )
        records = parse_sacct_records(output, fields)
        if len(records) != expected_count:
            continue
        return records
    if fatal:
        pytest.fail(
            f"Expected {expected_count} records for job {job_id}, got: {output}"
        )
    return records


def wait_for_script_progress(file_out, message):
    """Wait for a progress message from the resize job script.

    Fails as soon as the script reports a failure, rather than waiting out
    the timeout on a message that is never coming.
    """

    output = ""
    for t in atf.timer():
        output = atf.run_command_output(f"cat {file_out}", quiet=True)
        if MSG_FAILED in output:
            pytest.fail(f"The job script failed before reaching '{message}': {output}")
        if message in output:
            return
    pytest.fail(f"The job script never reported '{message}', got: {output}")


def _abort_on_failure(command):
    """Wrap a script command so its own failure is what gets reported.

    Without this a failed resize still falls through to the progress message
    below it, and the test only fails much later on an assertion that says
    nothing about which command broke.
    """

    if not command:
        return ""
    # The command is reported single-quoted so quotes in it can't break the
    # script or run inside the error message.
    reported = shlex.quote(command)
    return (
        f"{command} || {{ echo {shlex.quote(MSG_FAILED + ':')} {reported}; exit 1; }}"
    )


def submit_and_resize_job(name):
    """Submit a 2 node job, resize it to 1 node and leave it running.

    Returns the job id, the SLUID the job had before the resize, and the
    output file the script writes its progress messages to.
    """

    file_out = f"{name}_output"
    script = f"{name}.sh"
    do_resize = _abort_on_failure("scontrol update JobId=$SLURM_JOBID NumNodes=1")
    do_source_resize = _abort_on_failure(". slurm_job_${SLURM_JOBID}_resize.sh")
    atf.make_bash_script(
        script,
        # set -e is not usable here, as the USR1 trap interrupts the sleep
        # loop with a non-zero status.
        f"""trap 'received=1' USR1
received=0
echo "{MSG_READY}"
while [ $received -eq 0 ]; do
    sleep 1
done
{do_resize}
{do_source_resize}
rm -f slurm_job_${{SLURM_JOBID}}_resize.sh slurm_job_${{SLURM_JOBID}}_resize.csh
echo "{MSG_RESIZED}"
sleep infinity""",
    )

    job_id = atf.submit_job_sbatch(
        f"-N2 -t 5 -J {name} --output={file_out} {script}", fatal=True
    )
    wait_for_script_progress(file_out, MSG_READY)

    original_sluid = get_and_assert_sluid(job_id)
    atf.run_command(f"scancel --signal=USR1 --batch {job_id}", fatal=True)
    wait_for_script_progress(file_out, MSG_RESIZED)

    return job_id, original_sluid, file_out


def test_sacct_sluid():
    """Verify sacct shows SLUID and OriginalSLUID for a completed job."""

    job_id = atf.submit_job_sbatch("-n1 --wrap 'srun sleep infinity'", fatal=True)
    sluid = get_and_assert_sluid(job_id)
    atf.cancel_jobs([job_id], fatal=True)

    for t in atf.timer():
        output = atf.run_command_output(
            f"sacct -j {job_id} -X --noheader -o SLUID,OriginalSLUID",
            fatal=True,
        )
        if re.search(rf"{re.escape(sluid)}\s+{re.escape(sluid)}", output):
            break
    else:
        assert (
            False
        ), f"Expected SLUID={sluid} and OriginalSLUID={sluid} in sacct output: {output}"


def test_sacct_filter_by_sluid():
    """Verify sacct -j <SLUID> filters by SLUID."""

    job_id = atf.submit_job_sbatch("-n1 --wrap 'srun sleep infinity'", fatal=True)
    sluid = get_and_assert_sluid(job_id)
    atf.cancel_jobs([job_id], fatal=True)

    for t in atf.timer():
        output = atf.run_command_output(
            f"sacct -j {sluid} -X --noheader -o JobID,SLUID",
            fatal=True,
        )
        if re.search(rf"{job_id}\s+{re.escape(sluid)}", output):
            break
    else:
        assert (
            False
        ), f"Expected job {job_id} with SLUID {sluid} in sacct output: {output}"


def test_sacct_sluid_after_resize():
    """Verify a resize keeps the reported SLUID but gives the new record its own."""

    job_id, original_sluid, _ = submit_and_resize_job("resize")

    # squeue.1: the SLUID the command tools report survives a resize.
    assert (
        get_and_assert_sluid(job_id) == original_sluid
    ), f"The resize should keep the job's SLUID at {original_sluid}"

    atf.cancel_jobs([job_id], fatal=True)

    # The State a job_complete writes only reaches the database once
    # slurmctld has handed the message over.
    wait_for_dbd_agent_drained(job_id)

    # sacct.1: -D shows the record the resize split off as well as the one it
    # was split from, and OriginalSLUID correlates the two.
    fields = ["SLUID", "OriginalSLUID", "NNodes", "State"]
    pre_resize_record, resize_record = wait_for_sacct_records(job_id, fields, 2)

    assert (
        pre_resize_record["SLUID"] == original_sluid
    ), f"The first record should be the pre-resize {original_sluid}, got {pre_resize_record}"
    # sacct.1: the per-record SLUID changes on a resize.
    assert (
        resize_record["SLUID"] != original_sluid
    ), f"The resize record should take a new SLUID, still {original_sluid}"
    assert all(
        record["OriginalSLUID"] == original_sluid
        for record in (pre_resize_record, resize_record)
    ), f"Both records should keep OriginalSLUID {original_sluid}, got {pre_resize_record} and {resize_record}"

    node_counts = [
        int(record["NNodes"]) for record in (pre_resize_record, resize_record)
    ]
    assert node_counts == [
        2,
        1,
    ], f"Expected 2 nodes before the resize and 1 after it, got {node_counts}"

    # sacct.1: RS RESIZING says the resize is what ended the first record.
    states = [
        record["State"].split()[0] for record in (pre_resize_record, resize_record)
    ]
    assert states == [
        "RESIZING",
        "CANCELLED",
    ], f"Expected the records to end RESIZING and CANCELLED, got {states}"

    # sacct.1: without -D the most recent record is shown along with any
    # record a resize left in RESIZING. A resized job lists both.
    expected_sluids = [pre_resize_record["SLUID"], resize_record["SLUID"]]
    output = atf.run_command_output(
        f"sacct -j {job_id} -X -P --noheader -o SLUID", fatal=True
    )
    assert output.split() == expected_sluids, (
        f"Without -D sacct should list the resize record and the one it was "
        f"split from, got: {output}"
    )

    # sacct.1: --json ignores sorting and formatting arguments, but which
    # records are selected is unaffected.
    output = atf.run_command_output(f"sacct -j {job_id} -X --json", fatal=True)
    json_sluids = [job.get("sluid") for job in json.loads(output).get("jobs", [])]
    assert (
        json_sluids == expected_sluids
    ), f"sacct --json should list the same two records, got {json_sluids}"


def test_sacct_sluid_after_requeue():
    """Verify that requeue generates a new SLUID and OriginalSLUID."""

    job_id = atf.submit_job_sbatch(
        "-n1 --requeue --wrap 'srun sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    sluid_before = get_and_assert_sluid(job_id)

    # scontrol.1: a requeue puts the job back into pending state. Hold it
    # there so the assertion can't race the job being scheduled again.
    atf.run_command(
        f"scontrol requeuehold {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    sluid_after = wait_for_new_sluid(job_id, sluid_before)
    atf.run_command(
        f"scontrol release {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Cancel so it completes, then check sacct
    atf.cancel_jobs([job_id], fatal=True)

    # The State and Restarts a job_complete writes are only in the database
    # once slurmctld has handed the message over.
    wait_for_dbd_agent_drained(job_id)

    # sacct.1: a requeue leaves a duplicate record, so it takes -D to tell
    # whether the run before it was kept rather than overwritten.
    fields = ["SLUID", "OriginalSLUID", "Submit", "Restarts", "State"]
    before_record, after_record = wait_for_sacct_records(job_id, fields, 2)

    # sacct.1: RQ REQUEUED says the first record was ended by the requeue.
    states = [record["State"].split()[0] for record in (before_record, after_record)]
    assert states == [
        "REQUEUED",
        "CANCELLED",
    ], f"Expected the records to end REQUEUED and CANCELLED, got {states}"

    assert (
        before_record["SLUID"] == before_record["OriginalSLUID"] == sluid_before
    ), f"The first record should keep the pre-requeue {sluid_before}, got {before_record}"
    # sacct.1: OriginalSLUID changes if the job is requeued.
    assert (
        after_record["SLUID"] == after_record["OriginalSLUID"] == sluid_after
    ), f"The requeue record should carry {sluid_after}, got {after_record}"

    # sacct.1: "If a job is requeued, the submit time is reset."
    assert parse_sacct_time(after_record["Submit"]) > parse_sacct_time(
        before_record["Submit"]
    ), (
        f"The requeue should reset the submit time past "
        f"{before_record['Submit']}, got {after_record['Submit']}"
    )

    # sacct.1: without -D only the most recent record is shown, and a requeue
    # leaves nothing in RESIZING for the resize exception to apply to.
    output = atf.run_command_output(
        f"sacct -j {job_id} -X -P --noheader -o SLUID", fatal=True
    )
    assert output.split() == [
        sluid_after
    ], f"Without -D sacct should show only the requeue record, got: {output}"

    # sacct.1: Restarts counts how many times the job was requeued. Asserted on
    # both records, so a job_start landing on the wrong one is caught.
    restarts = [int(record["Restarts"]) for record in (before_record, after_record)]
    assert restarts == [
        0,
        1,
    ], f"Expected 0 restarts before the requeue and 1 after it, got {restarts}"


def test_sacct_json_sluid():
    """Verify sacct --json contains sluid and original_sluid fields."""

    job_id = atf.submit_job_sbatch("-n1 --wrap 'srun sleep infinity'", fatal=True)
    sluid = get_and_assert_sluid(job_id)
    atf.cancel_jobs([job_id], fatal=True)

    for t in atf.timer():
        output = atf.run_command_output(f"sacct -j {job_id} -X --json", fatal=True)
        data = json.loads(output)
        jobs = data.get("jobs", [])

        if len(jobs) != 1:
            logging.debug(f"Expecting 1 job, got {len(jobs)}")
            continue

        job_sluid = jobs[0].get("sluid", "")
        original_sluid = jobs[0].get("original_sluid", "")

        if job_sluid == sluid and original_sluid == sluid:
            break
    else:
        assert (
            False
        ), f"Expected sluid={sluid} and original_sluid={sluid} in sacct JSON, got {job_sluid} and {original_sluid}"
