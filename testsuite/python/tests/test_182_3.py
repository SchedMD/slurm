############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job KV output and updates through slurmrestd v0.0.46."""

import json
import shlex

import pytest
import tests.test_112_46 as rest

import atf

pytestmark = pytest.mark.slow

setup = rest.setup
slurm = rest.slurm
slurmdb = rest.slurmdb
non_admin = rest.non_admin


def test_job_kv(slurm, non_admin):
    """Verify slurmrestd returns a job's populated KV document unchanged."""

    kv = {
        "team": "science",
        "nested": {"attempt": 2, "enabled": True},
        "values": [1, None, "quote ' and slash \\"],
    }
    job_id = atf.submit_job_sbatch('--hold --wrap "sleep infinity"', fatal=True)
    atf.run_command(
        f"scontrol update JobId={job_id} KV={shlex.quote(json.dumps(kv))}",
        fatal=True,
    )

    response = slurm.slurm_v0046_get_job(str(job_id))
    assert not response.errors, f"Unexpected REST errors: {response.errors}"
    assert not response.warnings, f"Unexpected REST warnings: {response.warnings}"
    assert len(response.jobs) == 1, "Expected exactly the submitted job"
    assert response.jobs[0].job_id == job_id, "REST returned a different job"
    assert response.jobs[0].kv == kv, "REST should preserve the complete KV document"


def get_kv(slurmdb, job_id, show_kv=True):
    """Return the job's KV from slurmdbd, asking for it unless told not to."""
    flags = {"show_kv": "true"} if show_kv else {}
    response = slurmdb.slurmdb_v0046_get_jobs(step=str(job_id), **flags)
    assert not response.errors, f"Unexpected REST errors: {response.errors}"
    assert len(response.jobs) == 1, f"Expected job {job_id} in the database"
    return response.jobs[0].kv


def post_kv(slurmdb, job_id, kv):
    """Set a job's KV through slurmdbd, and return the REST errors."""
    from openapi_client.exceptions import ApiException
    from openapi_client.models.v0046_job_modify import V0046JobModify

    try:
        response = slurmdb.slurmdb_v0046_post_job(
            str(job_id), v0046_job_modify=V0046JobModify(kv=kv)
        )
    except ApiException as e:
        return [e]
    return response.errors


def test_job_kv_modify(slurmdb, non_admin):
    """Verify slurmrestd sets and clears the KV of an ended job only."""

    job_id = atf.submit_job_sbatch("--wrap 'sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_job_accounted(job_id, fatal=True)

    errors = post_kv(slurmdb, job_id, {"a": "running"})
    assert errors, "slurmrestd should refuse to set the KV of a running job"
    assert get_kv(slurmdb, job_id) == {}, "A refused update should leave KV alone"

    atf.cancel_jobs([job_id], fatal=True)
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    kv = {"a": "ended", "nested": {"b": [1, None]}}
    errors = post_kv(slurmdb, job_id, kv)
    assert not errors, f"slurmrestd should set the KV of an ended job: {errors}"
    assert get_kv(slurmdb, job_id) == kv, "REST should return the KV just set"
    assert (
        get_kv(slurmdb, job_id, show_kv=False) == {}
    ), "REST should only return the KV with show_kv"

    errors = post_kv(slurmdb, job_id, {})
    assert not errors, f"slurmrestd should clear the KV of an ended job: {errors}"
    assert get_kv(slurmdb, job_id) == {}, "An empty object should clear the KV"


def test_job_kv_modify_not_owner(slurmdb, non_admin):
    """Verify slurmrestd refuses to set the KV of another user's ended job."""

    job_id = atf.submit_job_sbatch(
        "--wrap true", user=atf.properties["slurm-user"], fatal=True
    )
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    errors = post_kv(slurmdb, job_id, {"a": 1})
    assert errors, "slurmrestd should refuse to set the KV of another user's job"

    # Like the batch script, KV is private to the owner, a coordinator or an
    # admin, so this user cannot read it back
    response = slurmdb.slurmdb_v0046_get_jobs(step=str(job_id), show_kv="true")
    assert not response.jobs, "slurmrestd should not return another user's job KV"
    output = atf.run_command_output(
        f"sacct -j {job_id} --kv", user=atf.properties["slurm-user"], fatal=True
    )
    assert output.splitlines()[-1:] == [
        "NONE"
    ], f"A refused update should leave KV alone: {output!r}"


def test_job_modify_keeps_kv(slurmdb, non_admin):
    """Verify a REST update of another job field leaves KV alone."""
    from openapi_client.models.v0046_job_modify import V0046JobModify

    kv = {"a": 1}
    job_id = atf.submit_job_sbatch("--wrap true", fatal=True)
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    errors = post_kv(slurmdb, job_id, kv)
    assert not errors, f"slurmrestd should set the KV of an ended job: {errors}"
    assert get_kv(slurmdb, job_id) == kv, "REST should return the KV just set"

    response = slurmdb.slurmdb_v0046_post_job(
        str(job_id), v0046_job_modify=V0046JobModify(extra="x")
    )
    assert not response.errors, f"Unexpected REST errors: {response.errors}"
    assert get_kv(slurmdb, job_id) == kv, "Updating extra should leave KV alone"


def test_job_kv_requires_job(slurmdb, non_admin):
    """Verify slurmdbd returns no job KV without a job to filter on."""

    job_id = atf.submit_job_sbatch("--wrap true", fatal=True)
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    errors = post_kv(slurmdb, job_id, {"a": 1})
    assert not errors, f"slurmrestd should set the KV of an ended job: {errors}"

    # slurmdbd refuses the query the way it refuses scripts and environments
    # without a job, and returns no jobs at all
    response = slurmdb.slurmdb_v0046_get_jobs(show_kv="true")
    assert not [
        job.job_id for job in response.jobs if job.kv
    ], "slurmdbd should not return job KV for every job at once"
