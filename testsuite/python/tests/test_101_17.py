############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job KV is not stored without AccountingStoreFlags=job_kv."""

import json
import shlex

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    atf.require_config_parameter_excludes("AccountingStoreFlags", "job_kv")
    atf.require_version(
        (26, 11), "bin/sacct", reason="Issue 51034: sacct --kv was added in 26.11"
    )
    atf.require_slurm_running()


def test_kv_not_stored():
    """Verify sacct --kv reports NONE for a job with KV."""

    job_id = atf.submit_job_sbatch(
        f"--kv={shlex.quote(json.dumps({'a': 1}))} --wrap true", fatal=True
    )
    atf.wait_for_job_accounted(job_id, field="End", value=r"\d", fatal=True)

    # The document would follow the job record in a later message, so give
    # it time to show up. Nothing should, so keep that well under the default
    # timeout.
    expected = [f"KV for {job_id}", "-" * 80, "NONE"]
    # Read as an admin, since only the owner, a coordinator or an admin may
    # read KV, and the test user may have no association
    for _ in atf.timer(timeout=10, xfail=True):
        output = atf.run_command_output(
            f"sacct -j {job_id} --kv", user=atf.properties["slurm-user"], fatal=True
        )
        if output.splitlines() != expected:
            break
    assert (
        output.splitlines() == expected
    ), f"sacct should report no KV for job {job_id}: {output!r}"
