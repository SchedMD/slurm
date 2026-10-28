############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test authorization for job KV and KVPatch updates."""

import shlex

import pytest
import test_108_21 as auth
from test_108_21 import (
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
