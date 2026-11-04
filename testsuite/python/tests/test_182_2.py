############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test authorization for job KV and KVPatch updates."""

import json
import shlex

import pytest
import tests.test_108_21 as auth
from tests.test_108_21 import (
    assert_access,
    effective_owner,
    privileged,
    scoped_actors,
    script_marker,
    slurm_user,
    submit_sbatch,
)

import atf

setup = auth.setup
actor = auth.actor


def job_kv(job_id):
    """Return the KV document of the job, or None if it has none."""
    jobs = atf.get_jobs(job_id, use_json=True, user=slurm_user, fatal=True)
    return jobs[job_id].get("kv") or None


@pytest.mark.skipif(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Issue 51034: KV was added in 26.11",
)
@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
@pytest.mark.parametrize("option", ["KV", "KVPatch"])
def test_update_job_kv(actor, option):
    """Verify only Privileged users or Effective Owners can set a job's KV."""
    if option == "KV":
        value = f'{{"marker":"{script_marker}"}}'
    else:
        value = f'[{{"op":"add","path":"/marker","value":"{script_marker}"}}]'
    job_id = submit_sbatch(actor["owner"], "-H --wrap 'sleep infinity'")
    result = atf.run_command(
        f"scontrol update jobid={job_id} {option}={shlex.quote(value)}",
        user=actor["user"],
    )

    assert_access(
        actor,
        result,
        actor["name"] in privileged + effective_owner,
        job_kv(job_id) == {"marker": script_marker},
    )


def sacct_kv(job_id):
    """Return the KV document sacct reports for the job, or None for NONE."""
    output = atf.run_command_output(
        f"sacct -j {job_id} --kv", user=slurm_user, fatal=True
    )
    kv = output.splitlines()[-1:]
    return json.loads(kv[0]) if kv and kv[0] != "NONE" else None


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Issue 51034: sacctmgr modify job KV= was added in 26.11",
)
@pytest.mark.parametrize("actor", scoped_actors, indirect=True)
def test_sacctmgr_modify_job_kv(actor):
    """Verify only Privileged users or the owner can set an ended job's KV.

    Unlike scontrol update job, sacctmgr modify job does not allow a
    Coordinator to change it.
    """
    value = json.dumps({"marker": script_marker})
    job_id = submit_sbatch(actor["owner"], "--wrap true")
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)
    result = atf.run_command(
        f"sacctmgr -i modify job where jobid={job_id} set KV={shlex.quote(value)}",
        user=actor["user"],
    )

    assert_access(
        actor,
        result,
        actor["name"] in privileged + ["owner"],
        sacct_kv(job_id) == {"marker": script_marker},
    )
