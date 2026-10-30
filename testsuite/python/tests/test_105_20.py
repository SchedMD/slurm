############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the sbatch, salloc and srun --kv option.

--kv attaches a KV metadata document to a job at submission. The clients refuse
anything that is not a JSON object before the job is submitted, and the
document can then be read back with scontrol.
"""

import json
import shlex

import pytest

import atf

# Nested objects, arrays, booleans, null and characters a shell treats
# specially must all come back unchanged.
KV = {
    "team": "science",
    "nested": {"attempt": 2, "enabled": True},
    "values": [1, None, "quote ' and slash \\"],
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    for command in ["sbatch", "salloc", "srun"]:
        atf.require_version(
            (26, 11),
            f"bin/{command}",
            reason=f"Issue 51034: {command} --kv was added in 26.11",
        )
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_slurm_running()


def kv_arg(kv):
    """Format a --kv=<json> argument, quoted for the shell."""

    return f"--kv={shlex.quote(json.dumps(kv))}"


def decode_kv(job):
    """Return the KV document a job carries in --json output, or None."""

    return job.get("kv") or None


def job_kv(job_id):
    return decode_kv(atf.get_jobs(job_id, use_json=True, fatal=True)[job_id])


def test_sbatch_kv():
    """sbatch --kv gives the job the document."""

    job_id = atf.submit_job_sbatch(f"{kv_arg(KV)} --hold --wrap true", fatal=True)
    assert job_kv(job_id) == KV, "sbatch --kv should set the job's KV document"


def test_salloc_kv():
    """salloc --kv gives the job the document."""

    job_id = atf.submit_job_salloc(f"{kv_arg(KV)} true", fatal=True)
    assert job_id, "salloc should have been granted an allocation"
    assert job_kv(job_id) == KV, "salloc --kv should set the job's KV document"


def test_srun_kv():
    """srun --kv gives the job it allocates the document."""

    job_id = atf.submit_job_srun(f"{kv_arg(KV)} true", fatal=True)
    assert job_id, "srun should have been granted an allocation"
    assert job_kv(job_id) == KV, "srun --kv should set the job's KV document"


def test_empty_kv():
    """An empty --kv= gives the job no document."""

    job_id = atf.submit_job_sbatch("--kv= --hold --wrap true", fatal=True)
    assert job_kv(job_id) is None, "--kv= should leave the job without KV"


@pytest.mark.parametrize("command", ["sbatch", "salloc", "srun"])
@pytest.mark.parametrize("value", ["{", "[]", '"text"'])
def test_invalid_kv_refused(command, value):
    """A value that is not a JSON object is refused before submission."""

    args = "--wrap true" if command == "sbatch" else "true"
    # No fatal=True: the refusal is what is being asserted.
    result = atf.run_command(f"{command} --kv={shlex.quote(value)} {args}")
    assert result["exit_code"] != 0, (
        f"{command} should refuse --kv={value}, got exit_code "
        f"{result['exit_code']} and stdout: {result['stdout']}"
    )
    assert (
        "Invalid --kv" in result["stderr"]
    ), f"The refusal should name --kv, got: {result['stderr']}"


def test_kv_in_batch_script():
    """#SBATCH --kv takes the same quoting as the command line."""

    script = f"{atf.module_tmp_path}/kv_job.sh"
    atf.make_bash_script(
        script,
        """#SBATCH --kv='{"a b": "c d", "n": [1, null]}'
true
""",
    )

    job_id = atf.submit_job_sbatch(f"--hold {script}", fatal=True)
    assert job_kv(job_id) == {
        "a b": "c d",
        "n": [1, None],
    }, "#SBATCH --kv should set the job's KV document"


def test_kv_hetjob():
    """Each hetjob component gets only the --kv given for it."""

    first = {"component": "first"}
    third = {"component": "third"}
    leader_id = atf.submit_job_sbatch(
        f"--hold {kv_arg(first)} -n1 : -n1 : -n1 {kv_arg(third)} --wrap true",
        fatal=True,
    )

    components = sorted(
        atf.get_jobs(leader_id, use_json=True, fatal=True).values(),
        key=lambda job: job["het_job_offset"]["number"],
    )
    assert [decode_kv(job) for job in components] == [
        first,
        None,
        third,
    ], "A hetjob component should not inherit --kv from the one before it"


def test_srun_kv_ignored_for_steps():
    """srun --kv inside an allocation does not change the job's document."""

    job_id = atf.submit_job_sbatch(
        f"{kv_arg(KV)} --wrap {shlex.quote('srun ' + kv_arg({'step': 1}) + ' true')}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    assert job_kv(job_id) == KV, "srun --kv for a step should not change job KV"
