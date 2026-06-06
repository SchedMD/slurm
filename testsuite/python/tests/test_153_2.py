############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom gpu files and custom gres")
    atf.require_version(
        (26, 5),
        component="bin/scontrol",
        reason="Ticket 24746: MetricsType only supported in 26.05+",
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_accounting(True)
    atf.require_config_parameter("AccountingStorageTres", "gres/gpu")
    atf.require_config_parameter("GresTypes", "gpu")
    atf.require_nodes(2, [("Gres", "gpu:4")])

    gpu_file = f"{str(atf.module_tmp_path)}/gpu"
    for i in range(0, 4):
        atf.run_command(f"touch {gpu_file}{i}")
    node_names = "node[1-2]"
    atf.require_config_parameter(
        "NodeName",
        f"{node_names} Name=gpu File={gpu_file}[0-3]",
        source="gres",
    )

    atf.set_config_parameter("PrivateData", None)
    atf.require_config_parameter("MetricsType", "metrics/openmetrics")

    atf.require_config_parameter(
        "PartitionName",
        {
            "a": {"Nodes": "ALL"},
            "b": {"Nodes": "ALL"},
        },
    )

    atf.require_slurm_running()


def get_metric_value(lines: str, name: str) -> str | None:
    for line in lines.splitlines():
        if line.startswith(name + " "):
            return line.split(" ", 1)[1]
    return None


def get_labeled_metric_value(
    lines: str, name: str, label_key: str, label_val: str
) -> str | None:
    pattern = rf"^{re.escape(name)}\{{[^}}]*{re.escape(label_key)}=\"{re.escape(label_val)}\"[^}}]*\}}\s+(\S+)\s*$"
    for line in lines.splitlines():
        m = re.match(pattern, line)
        if m:
            return m.group(1)
    return None


def assert_metric(output, name, predicate, msg):
    val = get_metric_value(output, name)
    assert val is not None, f"Missing {name} in output:\n{output}"
    assert predicate(val), f"{msg}: got {val}"


def assert_labeled_metric(output, name, label_key, label_val, predicate, msg):
    val = get_labeled_metric_value(output, name, label_key, label_val)
    assert (
        val is not None
    ), f'Missing {name}{{{label_key}="{label_val}"}} in output:\n{output}'
    assert predicate(val), f"{msg}: got {val}"


def test_alloc_metrics_nodes():
    """Test that metrics/nodes exposes per-node alloc metrics for CPUs, GPUs, and memory."""

    nodes_output = atf.request_slurmctld("metrics/nodes").text

    for metric in [
        "slurm_node_cpus_alloc",
        "slurm_node_gpus_alloc",
        "slurm_node_memory_alloc_bytes",
    ]:
        assert_labeled_metric(
            nodes_output,
            metric,
            "node",
            "node1",
            lambda v: int(v) == 0,
            f"Expected {metric} == 0 with no jobs",
        )

    assert_labeled_metric(
        nodes_output,
        "slurm_node_gpus",
        "node",
        "node1",
        lambda v: int(v) == 4,
        "Expected slurm_node_gpus == 4",
    )

    job_id = atf.submit_job_sbatch(
        "--gres=gpu:2 -N1 -n1 -p a -w node1 --wrap='srun sleep 300'"
    )
    atf.wait_for_job_state(job_id, "RUNNING")

    atf.repeat_until(
        lambda: get_labeled_metric_value(
            atf.request_slurmctld("metrics/nodes").text,
            "slurm_node_cpus_alloc",
            "node",
            "node1",
        ),
        lambda val: val and int(val) >= 1,
        fatal=True,
    )

    nodes_output = atf.request_slurmctld("metrics/nodes").text
    assert_labeled_metric(
        nodes_output,
        "slurm_node_cpus_alloc",
        "node",
        "node1",
        lambda v: int(v) >= 1,
        "Expected slurm_node_cpus_alloc >= 1 with running job",
    )
    assert_labeled_metric(
        nodes_output,
        "slurm_node_gpus_alloc",
        "node",
        "node1",
        lambda v: int(v) == 2,
        "Expected slurm_node_gpus_alloc == 2 with running job",
    )
    assert_labeled_metric(
        nodes_output,
        "slurm_node_memory_alloc_bytes",
        "node",
        "node1",
        lambda v: int(v) > 0,
        "Expected slurm_node_memory_alloc_bytes > 0 with running job",
    )


def test_alloc_metrics_jobs():
    """Test that metrics/jobs exposes aggregate alloc metrics for CPUs, GPUs, memory, and nodes."""

    job_id = atf.submit_job_sbatch("--gres=gpu:2 -N1 -n1 -p a --wrap='srun sleep 300'")
    atf.wait_for_job_state(job_id, "RUNNING")

    atf.repeat_until(
        lambda: get_metric_value(
            atf.request_slurmctld("metrics/jobs").text, "slurm_jobs_nodes_alloc"
        ),
        lambda val: val and int(val) >= 1,
        fatal=True,
    )

    jobs_output = atf.request_slurmctld("metrics/jobs").text
    assert_metric(
        jobs_output,
        "slurm_jobs_cpus_alloc",
        lambda v: int(v) >= 1,
        "Expected slurm_jobs_cpus_alloc >= 1",
    )
    assert_metric(
        jobs_output,
        "slurm_jobs_gpus_alloc",
        lambda v: int(v) == 2,
        "Expected slurm_jobs_gpus_alloc == 2",
    )
    assert_metric(
        jobs_output,
        "slurm_jobs_memory_alloc",
        lambda v: int(v) > 0,
        "Expected slurm_jobs_memory_alloc > 0",
    )
    assert_metric(
        jobs_output,
        "slurm_jobs_nodes_alloc",
        lambda v: int(v) == 1,
        "Expected slurm_jobs_nodes_alloc == 1",
    )

    atf.cancel_all_jobs(quiet=True)


def test_alloc_metrics_partitions():
    """Test per-partition alloc metrics for totals and running job allocations."""

    parts_output = atf.request_slurmctld("metrics/partitions").text

    assert_labeled_metric(
        parts_output,
        "slurm_partition_cpus",
        "partition",
        "a",
        lambda v: int(v) > 0,
        "Expected slurm_partition_cpus > 0",
    )
    assert_labeled_metric(
        parts_output,
        "slurm_partition_gpus",
        "partition",
        "a",
        lambda v: int(v) == 8,
        "Expected slurm_partition_gpus == 8",
    )

    job_id = atf.submit_job_sbatch("--gres=gpu:3 -N1 -n1 -p a --wrap='srun sleep 300'")
    atf.wait_for_job_state(job_id, "RUNNING")

    atf.repeat_until(
        lambda: get_labeled_metric_value(
            atf.request_slurmctld("metrics/partitions").text,
            "slurm_partition_nodes_alloc",
            "partition",
            "a",
        ),
        lambda val: val and int(val) >= 1,
        fatal=True,
    )

    parts_output = atf.request_slurmctld("metrics/partitions").text
    assert_labeled_metric(
        parts_output,
        "slurm_partition_jobs_cpus_alloc",
        "partition",
        "a",
        lambda v: int(v) >= 1,
        "Expected slurm_partition_jobs_cpus_alloc >= 1",
    )
    assert_labeled_metric(
        parts_output,
        "slurm_partition_jobs_gpus_alloc",
        "partition",
        "a",
        lambda v: int(v) == 3,
        "Expected slurm_partition_jobs_gpus_alloc == 3",
    )
    assert_labeled_metric(
        parts_output,
        "slurm_partition_jobs_memory_alloc",
        "partition",
        "a",
        lambda v: int(v) > 0,
        "Expected slurm_partition_jobs_memory_alloc > 0",
    )
    assert_labeled_metric(
        parts_output,
        "slurm_partition_nodes_alloc",
        "partition",
        "a",
        lambda v: int(v) >= 1,
        "Expected slurm_partition_nodes_alloc >= 1",
    )
    assert_labeled_metric(
        parts_output,
        "slurm_partition_nodes_cpus_alloc",
        "partition",
        "a",
        lambda v: int(v) >= 1,
        "Expected slurm_partition_nodes_cpus_alloc >= 1",
    )
    assert_labeled_metric(
        parts_output,
        "slurm_partition_nodes_mem_alloc",
        "partition",
        "a",
        lambda v: int(v) > 0,
        "Expected slurm_partition_nodes_mem_alloc > 0",
    )

    atf.cancel_all_jobs(quiet=True)


def test_alloc_metrics_jobs_users_accts():
    """Test per-user alloc metrics for CPUs, GPUs, memory, and nodes."""

    job_id = atf.submit_job_sbatch("--gres=gpu:1 -N1 -n1 -p a --wrap='srun sleep 300'")
    atf.wait_for_job_state(job_id, "RUNNING")

    username = atf.properties["test-user"]

    atf.repeat_until(
        lambda: get_labeled_metric_value(
            atf.request_slurmctld("metrics/jobs-users-accts").text,
            "slurm_user_jobs_nodes_alloc",
            "username",
            username,
        ),
        lambda val: val and int(val) >= 1,
        fatal=True,
    )

    ua_output = atf.request_slurmctld("metrics/jobs-users-accts").text
    assert_labeled_metric(
        ua_output,
        "slurm_user_jobs_cpus_alloc",
        "username",
        username,
        lambda v: int(v) >= 1,
        "Expected slurm_user_jobs_cpus_alloc >= 1",
    )
    assert_labeled_metric(
        ua_output,
        "slurm_user_jobs_gpus_alloc",
        "username",
        username,
        lambda v: int(v) == 1,
        "Expected slurm_user_jobs_gpus_alloc == 1",
    )
    assert_labeled_metric(
        ua_output,
        "slurm_user_jobs_memory_alloc",
        "username",
        username,
        lambda v: int(v) > 0,
        "Expected slurm_user_jobs_memory_alloc > 0",
    )
    assert_labeled_metric(
        ua_output,
        "slurm_user_jobs_nodes_alloc",
        "username",
        username,
        lambda v: int(v) == 1,
        "Expected slurm_user_jobs_nodes_alloc == 1",
    )

    atf.cancel_all_jobs(quiet=True)
