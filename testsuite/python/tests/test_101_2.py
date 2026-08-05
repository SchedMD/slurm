############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct's SLUID reporting across job resizes and requeues."""

import json
import logging
import re

import pytest

import atf

# Progress messages the resize job script writes to its output file.
MSG_READY = "Ready to get signaled"
MSG_RESIZED = "Resize done"
MSG_FAILED = "Resize scaffolding failed"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5), "bin/sacct", reason="Ticket 22180: SLUID availability added in 26.05+"
    )

    atf.require_accounting()
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()


def get_and_assert_sluid(job_id):
    """Get the SLUID for a given job_id via scontrol -d show job."""
    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid is not None, f"Job {job_id} has no SLUID"
    return sluid


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
    return f'{command} || {{ echo "{MSG_FAILED}: {command}"; exit 1; }}'


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
    """Verify that after a job resize, SLUID changes but OriginalSLUID is preserved."""

    job_id, original_sluid, _ = submit_and_resize_job("resize")

    atf.cancel_jobs([job_id], fatal=True)

    for t in atf.timer():
        output = atf.run_command_output(
            f"sacct -j {job_id} -X --noheader -o SLUID,OriginalSLUID",
            fatal=True,
        )

        # The most recent entry should have a new SLUID but the same OriginalSLUID
        lines = [line.strip() for line in output.strip().splitlines() if line.strip()]
        # After resize, sacct may show multiple entries. The last one has the new SLUID.
        last_line = lines[-1]
        match = re.match(r"(\S+)\s+(\S+)", last_line)

        if not match:
            logging.debug(f"Could not parse sacct output line: {last_line}")
            continue

        new_sluid = match.group(1)
        orig_sluid = match.group(2)

        if orig_sluid == original_sluid and new_sluid != original_sluid:
            break
    else:
        assert (
            False
        ), f"OriginalSLUID should be {original_sluid} and SLUID should have changed after resize, got {orig_sluid} and {new_sluid}"


def test_sacct_sluid_after_requeue():
    """Verify that requeue generates a new SLUID and OriginalSLUID."""

    job_id = atf.submit_job_sbatch(
        "-n1 --requeue --wrap 'srun sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    sluid_before = get_and_assert_sluid(job_id)

    # Requeue the job
    atf.run_command(
        f"scontrol requeue {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)

    # SLUID must have changed after requeue
    sluid_after = get_and_assert_sluid(job_id)
    assert (
        sluid_after != sluid_before
    ), f"SLUID should change after requeue, but still {sluid_before}"

    # Cancel so it completes, then check sacct
    atf.cancel_jobs([job_id], fatal=True)

    for t in atf.timer():
        output = atf.run_command_output(
            f"sacct -j {job_id} -X --noheader -o SLUID,OriginalSLUID",
            fatal=True,
        )
        # Both SLUID and OriginalSLUID should be the new value
        if re.search(rf"{re.escape(sluid_after)}\s+{re.escape(sluid_after)}", output):
            break
    else:
        assert (
            False
        ), f"Expected SLUID and OriginalSLUID both {sluid_after} after requeue: {output}"


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
