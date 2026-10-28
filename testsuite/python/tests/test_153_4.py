############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the partition_jobs_wait_part_node_limit metric counts the right jobs."""

import pytest

import atf

PARTITION = "metricpart"
METRIC = "slurm_partition_jobs_wait_part_node_limit"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 25769: the metric counted jobs pending for other "
        "reasons, and jobs that were no longer pending, before 26.11",
    )
    # Older clients and nodes cannot parse the MetricsType setting.
    for component in ("bin/scontrol", "sbin/slurmd"):
        atf.require_version((25, 11), component)
    # Metrics plugins are initialized at startup, not on reconfigure.
    if atf.properties["auto-config"] and atf.is_slurmctld_running(quiet=True):
        atf.stop_slurm()
    atf.require_nodes(2)
    atf.require_config_parameter("EnforcePartLimits", "NO")
    atf.require_config_parameter(
        "PartitionName",
        {PARTITION: {"Nodes": "ALL", "MaxNodes": 1, "MaxTime": 1}},
    )
    atf.require_config_parameter_excludes("CommunicationParameters", "disable_http")
    atf.require_config_parameter("MetricsType", "metrics/openmetrics")
    atf.require_config_parameter("MetricsAuthUsers", None)
    atf.require_config_parameter("PrivateData", None)
    atf.require_slurm_running()


def metric_value():
    """Current value of METRIC for PARTITION, or None if it is missing."""
    output = atf.request_slurmctld("metrics/partitions").text
    for line in output.splitlines():
        if line.startswith(f'{METRIC}{{partition="{PARTITION}"}} '):
            return int(line.split()[-1])
    return None


def test_metric_counts_only_pending_node_limit_jobs():
    """Only jobs pending with Reason=PartitionNodeLimit are counted.

    PartitionTimeLimit shares a bit with PartitionNodeLimit in the reason
    enum, so a bitwise test of the reason counted it too. A cancelled job
    keeps its last pending reason until it is purged, and must drop out.
    """
    node_job = atf.submit_job_sbatch(
        f"-p {PARTITION} -N2 --wrap='sleep infinity'", fatal=True
    )
    time_job = atf.submit_job_sbatch(
        f"-p {PARTITION} -t5 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        node_job, "PENDING", desired_reason="PartitionNodeLimit"
    ), f"Job {node_job} should be PENDING with Reason=PartitionNodeLimit"
    assert atf.wait_for_job_state(
        time_job, "PENDING", desired_reason="PartitionTimeLimit"
    ), f"Job {time_job} should be PENDING with Reason=PartitionTimeLimit"

    assert atf.repeat_until(
        metric_value, lambda v: v == 1
    ), f"{METRIC} should count only job {node_job}, got {metric_value()}"

    atf.cancel_jobs([node_job])
    assert atf.repeat_until(
        metric_value, lambda v: v == 0
    ), f"{METRIC} should not count cancelled job {node_job}, got {metric_value()}"
