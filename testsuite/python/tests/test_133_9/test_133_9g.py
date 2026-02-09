##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

from .conftest import cls1, submit_jobs_with_steps

pytestmark = pytest.mark.slow


def test_scontrol_operations(federation_setup, cleanup):
    """Test scontrol show jobs/steps with --local, -M, and --sibling options."""
    ids, cluster_map = submit_jobs_with_steps()
    ji0, ji1, ji2 = ids

    # --local show jobs: 1 RUNNING on cls1 + 2 REVOKED siblings = 3 records
    result = atf.run_command_output("scontrol --local show jobs -a", fatal=True)
    job_entries = re.findall(rf"JobId=({ji0}|{ji1}|{ji2})\b", result)
    assert (
        len(job_entries) == 3
    ), f"Expected exactly 3 job records with --local, found {len(job_entries)}:\n{result}"

    # --local show steps: only local job's steps. With stepmgr the steps are
    # only shown when the job is given (see squeue.1).
    local_job = [jid for jid, c in cluster_map.items() if c == cls1]
    assert len(local_job) == 1, f"Expected exactly 1 job on cls1, got {local_job}"
    result = atf.run_command_output(
        f"scontrol --local show steps {local_job[0]}", fatal=True
    )
    local_steps = re.findall(rf"StepId={local_job[0]}\.\d+", result)
    assert (
        len(local_steps) >= 2
    ), f"Expected at least 2 step records for local job {local_job[0]}, found {len(local_steps)}:\n{result}"

    # --local vs -Mcls1 equivalence
    local_result = atf.run_command_output("scontrol --local show jobs -a", fatal=True)
    m_result = atf.run_command_output(f"scontrol -M{cls1} show jobs -a", fatal=True)
    local_jobs = sorted(re.findall(rf"JobId=({ji0}|{ji1}|{ji2})\b", local_result))
    m_jobs = sorted(re.findall(rf"JobId=({ji0}|{ji1}|{ji2})\b", m_result))
    assert local_jobs == m_jobs, (
        f"--local and -M{cls1} should show same jobs:\n"
        f"--local: {local_jobs}\n-M{cls1}: {m_jobs}"
    )

    # Per-cluster steps of the job running in each cluster
    for jid, cluster in cluster_map.items():
        result = atf.run_command_output(
            f"scontrol -M{cluster} show steps {jid}", fatal=True
        )
        step_entries = re.findall(rf"StepId={jid}\.\d+", result)
        assert (
            len(step_entries) >= 2
        ), f"Expected at least 2 step records of {jid} on {cluster}, found {len(step_entries)}:\n{result}"
