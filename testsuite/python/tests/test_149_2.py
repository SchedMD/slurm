############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest
import logging
import re
import math

job_rss = 200  # MB
sleep_secs = 8


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmd",
        reason="Ticket 8473: ProfileInfluxDBFlags and ProfileInfluxDBExtraTags require 26.05",
    )
    atf.require_influxdb()
    atf.require_config_parameter(
        "ProfileInfluxDBFlags", "grouped_fields", source="acct_gather"
    )
    atf.require_config_parameter(
        "ProfileInfluxDBExtraTags",
        "cluster,sluid,uid,user",
        source="acct_gather",
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def profile_jobid(use_memory_program):
    """Submit one srun job with task profiling, return its jobid."""
    jobid = atf.submit_job_sbatch(
        f"--acctg-freq=1 --profile=task -t 1 "
        f'--wrap="srun {use_memory_program} {job_rss} {sleep_secs}"',
        fatal=True,
    )
    atf.wait_for_job_state(jobid, "COMPLETED", fatal=True)
    return jobid


def test_grouped_fields_measurement(profile_jobid):
    """Verify grouped_fields emits one 'task' measurement per sample."""

    logging.info(f"Retrieving grouped InfluxDB RSS metric for job {profile_jobid}")
    output = atf.request_influxdb(
        f"select max(RSS) from task where job = '{profile_jobid}' AND step = '0' "
        f"AND time > now() - 1h"
    )
    match = re.search(r"\s+(\d+)\s*$", output)
    assert (
        match is not None
    ), f"task measurement should expose an RSS field for job {profile_jobid}"
    assert math.isclose(int(match.group(1)) / 1024, job_rss, abs_tol=20)


def test_extra_tags_present(profile_jobid):
    """Verify cluster, sluid, uid and user tags are emitted on task."""

    logging.info("Retrieving tag keys on tasks")
    tag_keys = atf.request_influxdb("show tag keys from task")
    for key in ("cluster", "sluid", "uid", "user"):
        assert (
            key in tag_keys
        ), f"Expected '{key}' tag key on 'task' measurement; got:\n{tag_keys}"


def test_batch_step_name(profile_jobid):
    """Verify the batch step renders as 'batch' in the step tag."""

    output = atf.request_influxdb(
        f"show tag values from task with key = step where job = '{profile_jobid}'"
    )
    step_values = set(re.findall(r"^step\s+(\S+)\s*$", output, re.MULTILINE))
    assert (
        "batch" in step_values
    ), f"Expected 'batch' step tag for job {profile_jobid}; got:\n{output}"
    assert (
        "0" in step_values
    ), f"Expected '0' step tag for job {profile_jobid}; got:\n{output}"


def test_measurements_match_grouped_schema(profile_jobid):
    """Verify schema is grouped: 'task' measurement exists, 'RSS' doesn't."""

    output = atf.request_influxdb("show measurements")
    assert (
        "task" in output
    ), f"Expected 'task' measurement (grouped schema); got:\n{output}"
    assert (
        "RSS" not in output
    ), f"Did not expect 'RSS' as a measurement (vanilla schema); got:\n{output}"
