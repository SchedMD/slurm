############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job KV output through slurmrestd v0.0.46."""

import json
import shlex

import pytest
import test_112_46 as rest

import atf

pytestmark = pytest.mark.slow

setup = rest.setup
slurm = rest.slurm
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
