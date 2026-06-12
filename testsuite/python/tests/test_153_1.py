############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import atf
import pytest

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Dev 50538: MetricsType added in 25.11
    atf.require_version((25, 11), component="bin/scontrol")

    # Ensure exactly 3 nodes exist
    atf.require_nodes(3)

    # Ensure MetricsType is openmetrics
    atf.require_config_parameter("MetricsType", "metrics/openmetrics")

    # JWT is needed for HTTP auth to access metrics endpoints
    atf.require_config_parameter("AuthAltTypes", "auth/jwt")
    atf.require_config_parameter("AuthAltTypes", "auth/jwt", source="slurmdbd")

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


@pytest.fixture(scope="function")
def setup_metrics_test_case(request):
    mauth, mparam, pdata, use_slurm_user, xfail = request.param
    username = atf.properties["test-user"]
    if use_slurm_user:
        username = atf.properties["slurm-user"]

    # Restart Slurm to apply proper mode
    atf.stop_slurm()
    atf.set_config_parameter("MetricsAuthUsers", mauth)
    atf.set_config_parameter("MetricsParameters", mparam)
    atf.set_config_parameter("PrivateData", pdata)
    atf.require_slurm_running()

    # Submit a job as SlurmUser, and another as atf (normal) user
    job_id1 = atf.submit_job_sbatch(
        "-N1 -n1 -p debug --wrap='srun sleep infinity'",
        user=atf.properties["slurm-user"],
    )
    job_id2 = atf.submit_job_sbatch(
        "-N2 -n2 -p debug --wrap='srun sleep infinity'",
        user=atf.properties["test-user"],
    )
    atf.wait_for_job_state(job_id1, "RUNNING", user=atf.properties["slurm-user"])
    atf.wait_for_job_state(job_id2, "RUNNING", user=atf.properties["test-user"])

    # Allow slurmctld to update metrics after jobs submission
    # Note: Always execute this as admin user to ensure access to both jobs
    for t in atf.timer():
        resp = atf.request_slurmctld(
            "metrics/jobs", user=atf.properties["slurm-user"]
        ).text
        metric = _get_metric_value(resp, "slurm_jobs")
        if metric and int(metric) == 2:
            break
    else:
        pytest.fatal("Unable to get metrics/jobs properly")

    yield username, xfail


@pytest.mark.parametrize(
    # Parameters are:
    # - MetricsAuthUsers
    # - MetricsParameters
    # - PrivateData
    # - Submit job as SlurmUser? True or False
    # - Expect a permission error? True or False
    "setup_metrics_test_case",
    [
        # setup_metrics_test_case0
        (None, None, None, True, False),
        # setup_metrics_test_case1
        pytest.param(
            (None, None, "jobs", True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case2
        pytest.param(
            (None, "ignore_private_data", None, True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case3
        pytest.param(
            (atf.properties["slurm-user"], None, None, True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case4
        pytest.param(
            ("nonexisting_user", None, None, True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case5
        pytest.param(
            (None, "ignore_private_data", "jobs", True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case6
        pytest.param(
            (atf.properties["slurm-user"], None, "jobs", True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case7
        pytest.param(
            ("nonexisting_user", None, "jobs", True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case8
        pytest.param(
            (atf.properties["slurm-user"], "ignore_private_data", "jobs", True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case9
        pytest.param(
            ("nonexisting_user", "ignore_private_data", "jobs", True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case10
        pytest.param(
            (atf.properties["slurm-user"], "ignore_private_data", None, True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case11
        pytest.param(
            ("nonexisting_user", "ignore_private_data", None, True, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case12
        (None, None, None, False, False),
        # setup_metrics_test_case13
        pytest.param(
            (None, None, "jobs", False, True),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case14
        pytest.param(
            (None, "ignore_private_data", None, False, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case15
        pytest.param(
            (atf.properties["test-user"], None, None, False, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case16
        pytest.param(
            ("nonexisting_user", None, None, False, True),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case17
        pytest.param(
            (None, "ignore_private_data", "jobs", False, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case18
        pytest.param(
            (atf.properties["test-user"], None, "jobs", False, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case19
        pytest.param(
            ("nonexisting_user", None, "jobs", False, True),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case20
        pytest.param(
            (atf.properties["test-user"], "ignore_private_data", "jobs", False, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case21
        pytest.param(
            ("nonexisting_user", "ignore_private_data", "jobs", False, True),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case22
        pytest.param(
            (atf.properties["test-user"], "ignore_private_data", None, False, False),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
        # setup_metrics_test_case23
        pytest.param(
            ("nonexisting_user", "ignore_private_data", None, False, True),
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (26, 5),
                reason="Issue 50792: Auth for MetricsType added in 26.05",
            ),
        ),
    ],
    indirect=["setup_metrics_test_case"],
)
def test_http_metrics_openmetrics_endpoints(setup_metrics_test_case):
    username, xfail = setup_metrics_test_case

    if not xfail:
        # Root endpoint: print available metric endpoints
        r = atf.request_slurmctld("metrics", user=username)
        assert r.status_code == 200, "Expected HTTP access to metrics endpoints"
        assert r.text is not None, "Root endpoint is empty"

        # Partitions endpoint: expect slurm_partitions 2
        parts_output = atf.request_slurmctld("metrics/partitions", user=username).text
        parts_val = _get_metric_value(parts_output, "slurm_partitions")
        assert (
            parts_val is not None
        ), f"Missing slurm_partitions in output:\n{parts_output}"
        assert int(parts_val) == 2, f"Expected slurm_partitions 2, got {parts_val}"

        # Nodes endpoint: expect slurm_nodes 3
        nodes_output = atf.request_slurmctld("metrics/nodes", user=username).text
        nodes_val = _get_metric_value(nodes_output, "slurm_nodes")
        assert nodes_val is not None, f"Missing slurm_nodes in output:\n{nodes_output}"
        assert int(nodes_val) == 3, f"Expected slurm_nodes 3, got {nodes_val}"

        # Scheduler endpoint: expect a timestamp value
        sched_output = atf.request_slurmctld("metrics/scheduler", user=username).text
        sched_val = _get_metric_value(sched_output, "slurm_sched_stats_timestamp")
        assert (
            sched_val is not None
        ), f"Missing slurm_sched_stats_timestamp in output:\n{sched_output}"
        assert int(sched_val) > 0, f"Expected positive timestamp, got {sched_val}"

        # Jobs endpoint: expect total jobs == 2
        jobs_output = atf.request_slurmctld("metrics/jobs", user=username).text
        jobs_val = _get_metric_value(jobs_output, "slurm_jobs")
        assert jobs_val is not None, f"Missing slurm_jobs in output:\n{jobs_output}"
        assert int(jobs_val) == 2, f"Expected slurm_jobs == 2, got {jobs_val}"

        # Jobs-users-accts endpoint: expect current slurm-user shows 1 job
        jobs_ua_output = atf.request_slurmctld(
            "metrics/jobs-users-accts", user=username
        ).text
        jobs_ua_val = _get_labeled_metric_value(
            jobs_ua_output, "slurm_user_jobs", "username", username
        )
        assert (
            jobs_ua_val is not None
        ), f'Missing slurm_user_jobs for username="{username}" in output:\n{jobs_ua_output}'
        assert (
            int(jobs_ua_val) == 1
        ), f'Expected slurm_user_jobs{{username="{username}"}} == 1, got {jobs_ua_val}'
    else:
        # Root endpoint: expect no access (FORBIDDEN)
        r = atf.request_slurmctld("metrics", user=username)
        assert (
            r.status_code == 403
        ), "Expected HTTP FORBIDDEN code for metrics root endpoint"

        # Partitions endpoint: expect no access (FORBIDDEN)
        r = atf.request_slurmctld("metrics/partitions", user=username)
        assert r.status_code == 403, "Expected HTTP FORBIDDEN code for slurm_partitions"

        # Nodes endpoint: expect no access (FORBIDDEN)
        r = atf.request_slurmctld("metrics/nodes", user=username)
        assert r.status_code == 403, "Expected HTTP FORBIDDEN code for slurm_nodes"

        # Scheduler endpoint: expect no access (FORBIDDEN)
        r = atf.request_slurmctld("metrics/scheduler", user=username)
        assert (
            r.status_code == 403
        ), "Expected HTTP FORBIDDEN code for slurm_sched_stats"

        # Jobs endpoint: expect no access (FORBIDDEN)
        r = atf.request_slurmctld("metrics/jobs", user=username)
        assert r.status_code == 403, "Expected HTTP FORBIDDEN code for slurm_jobs"

        # Jobs-users-accts endpoint: expect no access (FORBIDDEN)
        r = atf.request_slurmctld("metrics/jobs-users-accts", user=username)
        assert r.status_code == 403, "Expected HTTP FORBIDDEN code for slurm_user_jobs"
