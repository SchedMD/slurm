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

    # slurm.conf.5: requeue_delay defaults to the credential lifetime, which
    # would leave every requeued job pending for two minutes.
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("requeue_delay", 0), source="slurm"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("bf_interval", 1), source="slurm"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("sched_interval", 1), source="slurm"
    )

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


def submit_and_resize_job(name, sbatch_args="", pre_resize="", post_resize=""):
    """Submit a 2 node job, resize it to 1 node and leave it running.

    Runs `pre_resize` before the resize and `post_resize` after it, so each
    ends up in a different accounting record. `pre_resize` has to complete on
    its own, as the resize only starts once it returns.

    Returns the job id, the SLUID the job had before the resize, and the
    output file the script writes its progress messages to.
    """

    file_out = f"{name}_output"
    script = f"{name}.sh"
    do_pre_resize = _abort_on_failure(pre_resize)
    do_resize = _abort_on_failure("scontrol update JobId=$SLURM_JOBID NumNodes=1")
    do_source_resize = _abort_on_failure(". slurm_job_${SLURM_JOBID}_resize.sh")
    do_post_resize = _abort_on_failure(post_resize)
    atf.make_bash_script(
        script,
        # set -e is not usable here, as the USR1 trap interrupts the sleep
        # loop with a non-zero status.
        f"""trap 'received=1' USR1
received=0
{do_pre_resize}
echo "{MSG_READY}"
while [ $received -eq 0 ]; do
    sleep 1
done
{do_resize}
{do_source_resize}
rm -f slurm_job_${{SLURM_JOBID}}_resize.sh slurm_job_${{SLURM_JOBID}}_resize.csh
{do_post_resize}
echo "{MSG_RESIZED}"
sleep infinity""",
    )

    job_id = atf.submit_job_sbatch(
        f"-N2 -t 5 -J {name} {sbatch_args} --output={file_out} {script}", fatal=True
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


@pytest.fixture(scope="module")
def resized_requeued_job():
    """Run a 2 node job through a resize and then a requeue.

    Returns the job id, the SLUID it started with, the SLUID the requeue gave
    it, and its three accounting records, oldest first.
    """

    job_id, original_sluid, file_out = submit_and_resize_job(
        "resize_requeue",
        sbatch_args="--requeue",
        pre_resize="srun -N2 -n2 true",
        post_resize="srun -N1 -n1 true",
    )

    # Clear the sentinel so the requeued run's progress can't be matched
    # against the output this run already wrote.
    atf.run_command(f"rm -f {file_out}", fatal=True)
    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # The resize only split off a new accounting record, so the SLUID the
    # command tools report is still the original one the requeue replaces.
    requeue_sluid = wait_for_new_sluid(job_id, original_sluid)

    # A requeue replays the script from the top, so only pre_resize runs again
    # in the requeued run. Wait for it to get back to the signal, otherwise the
    # cancel races its srun step being created.
    wait_for_script_progress(file_out, MSG_READY)
    atf.cancel_jobs([job_id], fatal=True)

    wait_for_dbd_agent_drained(job_id)

    fields = [
        "SLUID",
        "OriginalSLUID",
        "NNodes",
        "Submit",
        "Start",
        "Restarts",
        "State",
    ]
    # Not fatal here, so a requeue that overwrote a record fails the test that
    # asserts the count rather than erroring in setup, where the xfail below
    # would report it as an expected failure.
    records = wait_for_sacct_records(job_id, fields, 3, fatal=False)

    return job_id, original_sluid, requeue_sluid, records


@pytest.mark.slow
def test_sacct_sluid_after_resize_then_requeue(resized_requeued_job):
    """Verify requeuing a resized job keeps every accounting record distinct."""

    job_id, original_sluid, requeue_sluid, records = resized_requeued_job

    # The record the requeue used to overwrite has to still be there. This is
    # what the fix delivers, so it is asserted before anything reads a record.
    assert (
        len(records) == 3
    ), f"The requeue should leave the resize record in place, got {records}"
    pre_resize_record, resize_record, requeue_record = records

    # sacct.1: the per-record SLUID changes on both a resize and a requeue, so
    # all three records have to carry a distinct one.
    assert (
        pre_resize_record["SLUID"] == original_sluid
    ), f"The first record should keep the original SLUID {original_sluid}, got {pre_resize_record}"
    assert (
        requeue_record["SLUID"] == requeue_sluid
    ), f"The last record should take the requeue SLUID {requeue_sluid}, got {requeue_record}"
    assert resize_record["SLUID"] not in (original_sluid, requeue_sluid), (
        f"The resize record should have a SLUID of its own, got "
        f"{resize_record['SLUID']}"
    )

    # sacct.1: OriginalSLUID survives a resize and is reset by a requeue.
    assert (
        pre_resize_record["OriginalSLUID"]
        == resize_record["OriginalSLUID"]
        == original_sluid
    ), (
        f"The resize should leave OriginalSLUID={original_sluid} on both the "
        f"pre-resize and resize records, got {pre_resize_record} and "
        f"{resize_record}"
    )
    assert (
        requeue_record["OriginalSLUID"] == requeue_sluid
    ), f"The requeue should reset OriginalSLUID to the new SLUID, got {requeue_record}"

    # The requeued run gets the job's original -N2 again; scontrol.1 does not
    # say whether a NumNodes reduction carries over into a requeue.
    node_counts = [int(record["NNodes"]) for record in records]
    assert node_counts == [2, 1, 2], (
        f"Expected 2, 1 and 2 nodes for the pre-resize, resize and requeue "
        f"records, got {node_counts}"
    )

    # sacct.1: Restarts counts how many times the job was requeued or
    # restarted. Asserted on every record, as a job_start landing on the wrong
    # one shows up here as a count advancing on a record before the requeue.
    restarts = [int(record["Restarts"]) for record in records]
    assert restarts == [0, 0, 1], (
        f"Expected the requeue to count only on its own record, got "
        f"{restarts} for the pre-resize, resize and requeue records"
    )

    # sacct.1: RS RESIZING and RQ REQUEUED say what each record was left by.
    states = [record["State"].split()[0] for record in records]
    assert states == ["RESIZING", "REQUEUED", "CANCELLED"], (
        f"Expected the pre-resize, resize and requeue records to end "
        f"RESIZING, REQUEUED and CANCELLED, got {states}"
    )

    # faq.html#job_size: a resize generates a new record showing the job
    # resubmitted and restarted at the new size.
    assert parse_sacct_time(resize_record["Submit"]) > parse_sacct_time(
        pre_resize_record["Submit"]
    ), (
        f"The resize record should be submitted after the pre-resize one, got "
        f"{pre_resize_record['Submit']} and {resize_record['Submit']}"
    )
    assert parse_sacct_time(resize_record["Start"]) == parse_sacct_time(
        resize_record["Submit"]
    ), f"The resize record should start at its submit time, got {resize_record}"

    # sacct.1: "If a job is requeued, the submit time is reset."
    assert parse_sacct_time(requeue_record["Submit"]) > parse_sacct_time(
        resize_record["Submit"]
    ), (
        f"The requeue should reset the submit time past the resize "
        f"{resize_record['Submit']}, got {requeue_record['Submit']}"
    )

    # sacct.1: without -D the most recent record is shown along with any
    # record a resize left in RESIZING. So the pre-resize record is still
    # listed while the resize record, left REQUEUED, is not.
    output = atf.run_command_output(
        f"sacct -j {job_id} -X -P --noheader -o SLUID", fatal=True
    )
    assert output.split() == [pre_resize_record["SLUID"], requeue_sluid], (
        f"Without -D sacct should list the pre-resize and requeue records "
        f"only, got: {output}"
    )

    # sacct.1: -j accepts a SLUID in place of a job id. The record the requeue
    # used to destroy has to stay individually addressable.
    resize_fields = ["SLUID", "OriginalSLUID", "NNodes"]
    output = atf.run_command_output(
        f"sacct -j {resize_record['SLUID']} -D -X -P --noheader "
        f"-o {','.join(resize_fields)}",
        fatal=True,
    )
    assert parse_sacct_records(output, resize_fields) == [
        {
            "SLUID": resize_record["SLUID"],
            "OriginalSLUID": original_sluid,
            "NNodes": "1",
        }
    ], f"Expected only the resize record when filtering on its SLUID, got: {output}"

    # Every step has to name one of the three records. The requeue used to
    # overwrite the resize record, which left steps naming a SLUID no record
    # carried. Which record a step lands under is a separate, still open bug
    # (see test_sacct_steps_after_resize_then_requeue), so this asserts only
    # that no step is left pointing at nothing.
    step_fields = ["JobID", "SLUID"]
    output = atf.run_command_output(
        f"sacct -j {job_id} -D -P --noheader -o {','.join(step_fields)}",
        fatal=True,
    )
    record_sluids = {record["SLUID"] for record in records}
    orphans = [
        record
        for record in parse_sacct_records(output, step_fields)
        if "." in record["JobID"] and record["SLUID"] not in record_sluids
    ]
    assert not orphans, (
        f"Every step should belong to one of {sorted(record_sluids)}, got "
        f"orphaned steps {orphans} in: {output}"
    )


@pytest.mark.slow
@pytest.mark.xfail(
    reason="Issue 51109: a step started after a resize is filed under the "
    "pre-resize record",
)
def test_sacct_steps_after_resize_then_requeue(resized_requeued_job):
    """Verify each run's srun step stays under its own accounting record."""

    job_id, original_sluid, requeue_sluid, records = resized_requeued_job
    pre_resize_record, resize_record, requeue_record = records

    # squeue.1: the database tracks a separate SLUID per accounting record, so
    # every run's steps have to stay under its own record rather than being
    # absorbed into the ones the earlier runs left behind. The resize record's
    # step is the one the requeue used to orphan.
    step_fields = ["JobID", "SLUID", "NNodes"]
    expected_steps = {
        (pre_resize_record["SLUID"], 2),
        (resize_record["SLUID"], 1),
        (requeue_sluid, 2),
    }
    output = ""
    steps = set()
    # xfail: the timeout below is the expected outcome while the bug is open.
    for t in atf.timer(xfail=True):
        output = atf.run_command_output(
            f"sacct -j {job_id} -D -P --noheader -o {','.join(step_fields)}",
            fatal=True,
        )
        # Only the srun steps are per-record; batch and extern span the resize.
        steps = {
            (record["SLUID"], int(record["NNodes"]))
            for record in parse_sacct_records(output, step_fields)
            if record["JobID"].partition(".")[2].isdigit()
        }
        # Compared exactly so a step landing under the wrong record fails.
        if steps == expected_steps:
            break
    else:
        pytest.fail(
            f"Expected one srun step under each record, {sorted(expected_steps)}, "
            f"got {sorted(steps)} in: {output}"
        )
