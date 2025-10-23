############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import atf
import pytest


HTTP_HOST = "127.0.0.1"
HTTP_PORT = 6817


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Dev 50538: Metrics added in 25.11
    atf.require_version((25, 11))

    # Ensure exactly 3 nodes exist
    atf.require_nodes(3)

    # Ensure PrivateData is not set (unset it explicitly)
    atf.set_config_parameter("PrivateData", None)

    # Ensure MetricsType is openmetrics
    atf.require_config_parameter("MetricsType", "metrics/openmetrics")

    # Define two partitions: debug and power
    atf.require_config_parameter(
        "PartitionName",
        {
            "debug": {"Nodes": "ALL"},
            "power": {"Nodes": "ALL"},
        },
    )

    # Start/reconfigure Slurm
    atf.require_slurm_running()


def _curl(path: str) -> str:
    cmd = f"curl http://{HTTP_HOST}:{HTTP_PORT}/{path}"
    res = atf.run_command(cmd, user=atf.properties["slurm-user"], fatal=True)
    return res["stdout"]


def _get_metric_value(lines: str, name: str) -> str | None:
    for line in lines.splitlines():
        if line.startswith(name + " "):
            return line.split(" ", 1)[1]
    return None


def _get_labeled_metric_value(
    lines: str, name: str, label_key: str, label_val: str
) -> str | None:
    pattern = rf"^{re.escape(name)}\{{[^}}]*{re.escape(label_key)}=\"{re.escape(label_val)}\"[^}}]*\}}\s+(\d+)\s*$"
    for line in lines.splitlines():
        m = re.match(pattern, line)
        if m:
            return m.group(1)
    return None


def test_http_metrics_openmetrics_endpoints():
    # Submit a simple job so jobs and jobs-users-accts metrics reflect 1 job
    job_id = atf.submit_job_sbatch("-N2 -n2 -p debug --wrap='srun sleep 1'")
    atf.wait_for_job_state(job_id, "RUNNING")

    # Allow slurmctld to update metrics after job submission
    retries = 2
    for _ in range(retries):
        jobs_output = _curl("metrics/jobs")
        val = _get_metric_value(jobs_output, "slurm_jobs")
        if val and int(val) >= 1:
            break
        time.sleep(0.2)

    # partitions endpoint: expect slurm_partitions 2
    parts_output = _curl("metrics/partitions")
    parts_val = _get_metric_value(parts_output, "slurm_partitions")
    assert parts_val is not None, f"Missing slurm_partitions in output:\n{parts_output}"
    assert int(parts_val) == 2, f"Expected slurm_partitions 2, got {parts_val}"

    # nodes endpoint: expect slurm_nodes 3
    nodes_output = _curl("metrics/nodes")
    nodes_val = _get_metric_value(nodes_output, "slurm_nodes")
    assert nodes_val is not None, f"Missing slurm_nodes in output:\n{nodes_output}"
    assert int(nodes_val) == 3, f"Expected slurm_nodes 3, got {nodes_val}"

    # scheduler endpoint: expect a timestamp value
    sched_output = _curl("metrics/scheduler")
    sched_val = _get_metric_value(sched_output, "slurm_sched_stats_timestamp")
    assert (
        sched_val is not None
    ), f"Missing slurm_sched_stats_timestamp in output:\n{sched_output}"
    assert int(sched_val) > 0, f"Expected positive timestamp, got {sched_val}"

    # jobs endpoint: expect total jobs >= 1
    jobs_output = _curl("metrics/jobs")
    jobs_val = _get_metric_value(jobs_output, "slurm_jobs")
    assert jobs_val is not None, f"Missing slurm_jobs in output:\n{jobs_output}"
    assert int(jobs_val) >= 1, f"Expected slurm_jobs >= 1, got {jobs_val}"

    # jobs-users-accts endpoint: expect current slurm-user shows 1 job
    jobs_ua_output = _curl("metrics/jobs-users-accts")
    username = "atf"
    jobs_ua_val = _get_labeled_metric_value(
        jobs_ua_output, "slurm_user_jobs", "username", username
    )
    assert (
        jobs_ua_val is not None
    ), f'Missing slurm_user_jobs for username="{username}" in output:\n{jobs_ua_output}'
    assert (
        int(jobs_ua_val) >= 1
    ), f'Expected slurm_user_jobs{{username="{username}"}} >= 1, got {jobs_ua_val}'
