############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test SPANK plugins that link against libslurm.

The plugin logs what spank_get_item() and slurm_load_job() report on every task
it runs on. Calling slurm_load_job() there deadlocks unless slurmstepd drops
its auth setuid lock before the SPANK stack (task.c:spank_user_task).
"""

import re

import pytest

import atf

# From slurm/slurm.h
NO_VAL = 0xFFFFFFFE
SLURM_BATCH_SCRIPT = 0xFFFFFFFB

TAG = "IT_RAN"
JOB_SCRIPT = "job.sh"

# Lines logged by the plugin. rc= only on the source=*_error ones.
LOG_RE = re.compile(
    r"caller=(?P<caller>\S+) "
    r"source=(?P<source>\S+) "
    r"self_job_id=(?P<self_job_id>\d+) "
    r"self_step_id=(?P<self_step_id>\d+) "
    r"job_id=(?P<job_id>\d+) "
    r"array_job_id=(?P<array_job_id>\d+) "
    r"array_task_id=(?P<array_task_id>\d+)"
    r"(?: rc=(?P<rc>-?\d+))?"
)


@pytest.fixture(scope="module", autouse=True)
def setup(spank_plugin):
    """Setup test environment once for all tests"""

    # The plugin is inert unless SPANK_HOOK_LOG_JOB_INFO is set, so no args needed
    atf.require_config_parameter(
        "required", spank_plugin, delimiter=" ", source="plugstack"
    )

    # Two nodes to run both tasks of the job array at the same time
    atf.require_nodes(2)
    atf.require_slurm_running()


def get_spank_records(spank_tmp):
    """Returns the lines logged by the SPANK plugin as a list of dicts."""

    log_path = f"{spank_tmp}/spank_job_info.log"
    content = atf.run_command_output(f"cat {log_path} 2>/dev/null", quiet=True)

    records = []
    for match in LOG_RE.finditer(content):
        record = match.groupdict()
        for key, value in record.items():
            if key not in ("caller", "source") and value is not None:
                record[key] = int(value)
        records.append(record)

    return records


def wait_for_spank_records(spank_tmp, condition):
    """Polls the log until condition(records) is met, returns the records.

    Not fatal on timeout: the assertions of the caller report what was logged.
    """

    atf.repeat_until(lambda: get_spank_records(spank_tmp), condition)

    return get_spank_records(spank_tmp)


def assert_no_spank_errors(records):
    """Asserts that the SPANK plugin didn't report any error."""

    errors = [record for record in records if record["source"].endswith("_error")]
    assert not errors, f"The SPANK plugin reported errors: {errors}"


def assert_task_logged_once(records, source, job_id, step_id):
    """Returns the only record logged by the given task, asserting there is one."""

    step_name = "batch script" if step_id == SLURM_BATCH_SCRIPT else f"step {step_id}"
    matches = [
        record
        for record in records
        if record["source"] == source
        and record["self_job_id"] == job_id
        and record["self_step_id"] == step_id
    ]
    assert len(matches) == 1, (
        f"The SPANK plugin should log {source} once for the {step_name} of job "
        f"{job_id}, but logged it {len(matches)} times. Records: {records}"
    )

    return matches[0]


def assert_tag_count(file_name, count):
    """Asserts that the output file has the tag printed the expected times."""

    assert atf.wait_for_file(file_name), f"Output file {file_name} was never created"

    # Wait for the tag, but assert the exact count to catch extra ones
    atf.repeat_command_until(
        f"cat {file_name}", lambda results: results["stdout"].count(TAG) >= count
    )
    content = atf.run_command_output(f"cat {file_name}")
    assert content.count(TAG) == count, (
        f"Output file {file_name} should contain '{TAG}' exactly {count} time(s), "
        f"but contains: {content}"
    )


def test_sbatch(spank_tmp):
    """Test the job info a SPANK plugin sees on a batch job and on its step."""

    output_file = "job.out"
    atf.make_bash_script(JOB_SCRIPT, f"srun echo {TAG}")

    job_id = atf.submit_job_sbatch(
        f"-N1 -t1 --no-requeue -o {output_file} {JOB_SCRIPT}",
        env_vars="SPANK_HOOK_LOG_JOB_INFO=1",
        fatal=True,
    )

    # A task deadlocked in the SPANK stack never completes
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "JobState") == "COMPLETED"
    ), f"Job {job_id} should have COMPLETED"

    assert_tag_count(output_file, 1)

    # The plugin runs on the batch script task and on the step task
    expected_steps = {SLURM_BATCH_SCRIPT, 0}
    records = wait_for_spank_records(
        spank_tmp,
        lambda records: {
            record["self_step_id"]
            for record in records
            if record["source"] == "spank_get_item"
        }
        >= expected_steps,
    )
    assert records, "The SPANK plugin should have logged the job info, but didn't"
    assert_no_spank_errors(records)

    for step_id in expected_steps:
        item = assert_task_logged_once(records, "spank_get_item", job_id, step_id)

        assert (
            item["job_id"] == job_id
        ), f"spank_get_item should report job id {job_id}, but reported {item['job_id']}"
        assert item["array_task_id"] == NO_VAL, (
            f"spank_get_item should report no array task id ({NO_VAL}) for the "
            f"non-array job {job_id}, but reported {item['array_task_id']}"
        )
        # stepd_step_rec_create() sets it to the job id for a step, 0 for batch
        assert item["array_job_id"] in (0, job_id), (
            f"spank_get_item should report an array job id of 0 or {job_id} for "
            f"the non-array job {job_id}, but reported {item['array_job_id']}"
        )

        loaded = assert_task_logged_once(records, "load_job", job_id, step_id)

        assert loaded["job_id"] == job_id, (
            f"slurm_load_job() should return the record of job {job_id}, but "
            f"returned the one of job {loaded['job_id']}"
        )
        assert loaded["array_job_id"] == 0, (
            f"slurm_load_job() should report no array job id for the non-array "
            f"job {job_id}, but reported {loaded['array_job_id']}"
        )
        assert loaded["array_task_id"] == NO_VAL, (
            f"slurm_load_job() should report no array task id ({NO_VAL}) for the "
            f"non-array job {job_id}, but reported {loaded['array_task_id']}"
        )


def test_sbatch_array(spank_tmp):
    """Test the job info a SPANK plugin sees on the tasks of a job array."""

    array_size = 2
    atf.make_bash_script(JOB_SCRIPT, f"srun echo {TAG}")

    # One output file per task: a shared one is truncated by each task when it
    # starts, so they race to wipe each other's output (ticket 21331)
    array_job_id = atf.submit_job_sbatch(
        f"--array=1-{array_size} -N1 -t1 --no-requeue -o %A_%a.out {JOB_SCRIPT}",
        env_vars="SPANK_HOOK_LOG_JOB_INFO=1",
        fatal=True,
    )

    # A task deadlocked in the SPANK stack never leaves the queue
    assert atf.repeat_command_until(
        f"squeue -h -j {array_job_id}", lambda results: results["stdout"].strip() == ""
    ), f"The tasks of job array {array_job_id} should have finished"

    # Every finished task has its own job record
    job_ids = {}
    for job_id, job in atf.get_jobs(job_id=array_job_id).items():
        if job.get("ArrayJobId") != array_job_id:
            continue

        job_ids[job["ArrayTaskId"]] = job_id
        assert job["JobState"] == "COMPLETED", (
            f"Task {job['ArrayTaskId']} of job array {array_job_id} should have "
            f"COMPLETED, but is in state {job['JobState']}"
        )

    expected_task_ids = list(range(1, array_size + 1))
    assert sorted(job_ids) == expected_task_ids, (
        f"Job array {array_job_id} should have the tasks {expected_task_ids}, "
        f"but found {sorted(job_ids)}"
    )

    for task_id in job_ids:
        assert_tag_count(f"{array_job_id}_{task_id}.out", 1)

    # The plugin runs on the batch script and step tasks of every array task
    expected_tasks = {
        (task_id, step_id) for task_id in job_ids for step_id in (SLURM_BATCH_SCRIPT, 0)
    }
    records = wait_for_spank_records(
        spank_tmp,
        lambda records: {
            (record["array_task_id"], record["self_step_id"])
            for record in records
            if record["source"] == "spank_get_item"
        }
        >= expected_tasks,
    )
    assert records, "The SPANK plugin should have logged the job info, but didn't"
    assert_no_spank_errors(records)

    for task_id, job_id in job_ids.items():
        for step_id in (SLURM_BATCH_SCRIPT, 0):
            item = assert_task_logged_once(records, "spank_get_item", job_id, step_id)

            assert item["array_job_id"] == array_job_id, (
                f"spank_get_item should report array job id {array_job_id} for "
                f"job {job_id}, but reported {item['array_job_id']}"
            )
            assert item["array_task_id"] == task_id, (
                f"spank_get_item should report array task id {task_id} for job "
                f"{job_id}, but reported {item['array_task_id']}"
            )

            # Asking about a task returns the whole array, check its own record
            loaded = [
                record
                for record in records
                if record["source"] == "load_job"
                and record["self_job_id"] == job_id
                and record["self_step_id"] == step_id
                and record["job_id"] == job_id
            ]
            assert len(loaded) == 1, (
                f"slurm_load_job() should return the record of job {job_id} "
                f"once, but returned it {len(loaded)} times. Records: {records}"
            )
            assert loaded[0]["array_job_id"] == array_job_id, (
                f"slurm_load_job() should report array job id {array_job_id} for "
                f"job {job_id}, but reported {loaded[0]['array_job_id']}"
            )
            assert loaded[0]["array_task_id"] == task_id, (
                f"slurm_load_job() should report array task id {task_id} for job "
                f"{job_id}, but reported {loaded[0]['array_task_id']}"
            )

    # Any other record must belong to the same array
    for record in [record for record in records if record["source"] == "load_job"]:
        assert record["job_id"] in job_ids.values(), (
            f"slurm_load_job() should only return records of the tasks of job "
            f"array {array_job_id} ({sorted(job_ids.values())}), but returned "
            f"the one of job {record['job_id']}"
        )
        assert record["array_job_id"] == array_job_id, (
            f"slurm_load_job() should only return records with array job id "
            f"{array_job_id}, but returned {record}"
        )
