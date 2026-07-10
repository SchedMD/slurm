############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for the partition MaxNodes limit with task-count jobs."""

import os
import re

import pytest

import atf

pytestmark = pytest.mark.slow

test_name = os.path.splitext(os.path.basename(__file__))[0]
PARTITION_NAME = "max_nodes_part"
MAX_NODES = 1
EXPLICIT_MAX_NODES = 1
RAISED_MAX_NODES = 2
ROUNDUP_PARTITION_NAME = "roundup_part"
ROUNDUP_MAX_NODES = 2
UPDATE_PARTITION_NAME = "update_part"
UPDATE_MAX_NODES = 2
UPDATE_TASKS = 8


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter("Name", {"gpu": {"File": "/dev/null"}}, source="gres")
    atf.require_config_parameter("EnforcePartLimits", "ALL")
    atf.require_nodes(
        2,
        [
            ("Sockets", 1),
            ("CoresPerSocket", 4),
            ("ThreadsPerCore", 2),
            ("CPUs", 8),
            ("RealMemory", 1024),
            ("Gres", "gpu:1"),
        ],
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def partition():
    created = []

    def _create(name, max_nodes):
        nodes = list(atf.get_nodes().keys())
        node_range = atf.node_list_to_range(nodes[0:2])

        atf.run_command(
            f"scontrol create PartitionName={name} "
            f"Nodes={node_range} MaxNodes={max_nodes}",
            fatal=True,
            user=atf.properties["slurm-user"],
        )
        created.append(name)

    yield _create

    atf.cancel_jobs(atf.properties["submitted-jobs"], quiet=True)
    for name in created:
        atf.run_command(
            f"scontrol delete PartitionName={name}",
            fatal=True,
            user=atf.properties["slurm-user"],
        )


@pytest.fixture(scope="function")
def max_nodes_partition(partition):
    partition(PARTITION_NAME, MAX_NODES)


@pytest.fixture(scope="function")
def roundup_partition(partition):
    partition(ROUNDUP_PARTITION_NAME, ROUNDUP_MAX_NODES)


@pytest.fixture(scope="function")
def update_partition(partition):
    partition(UPDATE_PARTITION_NAME, UPDATE_MAX_NODES)


@pytest.fixture(scope="function")
def no_enforce_part_limits():
    atf.set_config_parameter("EnforcePartLimits", "NO")

    yield

    atf.set_config_parameter("EnforcePartLimits", "ALL")


@pytest.fixture(scope="function")
def enforce_part_limits(request):
    atf.set_config_parameter("EnforcePartLimits", request.param)

    yield

    atf.set_config_parameter("EnforcePartLimits", "ALL")


def test_job_within_max_nodes(max_nodes_partition):
    """Verify a job whose derived node count is within MaxNodes runs"""

    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION_NAME} -n2 --ntasks-per-node=2 --gres=gpu:1"
        f' -J {test_name} -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert (
        len(node_list) == MAX_NODES
    ), f"Job should allocate {MAX_NODES} node(s), got {len(node_list)}: {node_list}"

    num_tasks = atf.get_job_parameter(job_id, "NumTasks")
    assert num_tasks == 2, f"Job should have been granted 2 tasks, got {num_tasks}"


def test_job_roundup_within_max_nodes(roundup_partition):
    """Verify a job needing several nodes runs when ROUNDUP is within MaxNodes"""

    job_id = atf.submit_job_sbatch(
        f"-p {ROUNDUP_PARTITION_NAME} -n3 --ntasks-per-node=2 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(node_list) == ROUNDUP_MAX_NODES, (
        f"Job should allocate ROUNDUP(3, 2)={ROUNDUP_MAX_NODES} node(s), "
        f"got {len(node_list)}: {node_list}"
    )


@pytest.mark.parametrize(
    "ntask_option",
    [
        None,
        "ntasks-per-socket",
        "ntasks-per-core",
        "ntasks-per-gpu",
    ],
)
def test_implicit_max_nodes_not_enforced(ntask_option, max_nodes_partition):
    """Verify a job whose derived node count exceeds MaxNodes still runs"""

    ntask_flag = f" --{ntask_option}=2" if ntask_option else ""
    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION_NAME} -n2{ntask_flag} --gres=gpu:1 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert (
        len(node_list) == MAX_NODES
    ), f"Job should allocate {MAX_NODES} node(s), got {len(node_list)}: {node_list}"

    num_tasks = atf.get_job_parameter(job_id, "NumTasks")
    assert num_tasks == 2, f"Job should have been granted 2 tasks, got {num_tasks}"


@pytest.mark.parametrize("enforce_part_limits", ["ALL", "NO"], indirect=True)
@pytest.mark.skipif(
    atf.get_config_parameter("SelectType", live=False) == "select/linear",
    reason="Ticket 25443: --ntasks-per-socket is not supported with select/linear",
)
def test_implicit_max_nodes_still_clamped(enforce_part_limits, max_nodes_partition):
    """Verify a job that cannot pack into MaxNodes is never run within it"""

    job_args = (
        f"-p {PARTITION_NAME} -n2 --ntasks-per-socket=1 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null'
    )
    result = atf.run_command(f"sbatch {job_args}", xfail=True)

    # Register the job if the submission unexpectedly succeeded, so that the
    # fixture cancels it before deleting the partition
    if match := re.search(r"Submitted \S+ job (\d+)", result["stdout"]):
        atf.properties["submitted-jobs"].append(int(match.group(1)))

    assert (
        result["exit_code"] != 0
    ), "A job needing more nodes than MaxNodes should not be accepted"
    assert "Requested node configuration is not available" in result["stderr"], (
        f"Expected the derived node count to be clamped to MaxNodes, "
        f"got: {result['stderr'].strip()}"
    )

    atf.run_command(
        f"scontrol update PartitionName={PARTITION_NAME} MaxNodes={RAISED_MAX_NODES}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    job_id = atf.submit_job_sbatch(job_args, fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(node_list) == RAISED_MAX_NODES, (
        f"Job should allocate {RAISED_MAX_NODES} node(s), "
        f"got {len(node_list)}: {node_list}"
    )


def test_job_exceeds_max_nodes(max_nodes_partition):
    """Verify a task-count job needing more nodes than MaxNodes is rejected"""

    result = atf.run_command(
        f"sbatch -p {PARTITION_NAME} -n2 --ntasks-per-node=1 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        xfail=True,
    )

    # Register the job if the submission unexpectedly succeeded, so that the
    # fixture cancels it before deleting the partition
    if match := re.search(r"Submitted \S+ job (\d+)", result["stdout"]):
        atf.properties["submitted-jobs"].append(int(match.group(1)))

    assert result["exit_code"] != 0, "Expected sbatch to fail due to MaxNodes limit"
    assert (
        "Node count specification invalid" in result["stderr"]
    ), f"Expected MaxNodes rejection message, got: {result['stderr'].strip()}"


def test_job_within_max_nodes_not_limited(no_enforce_part_limits, max_nodes_partition):
    """Verify a job within MaxNodes is not held with PartitionNodeLimit"""

    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION_NAME} -n2 --ntasks-per-node=2 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    reasons = set()
    for _ in atf.timer(fatal=True):
        job = atf.get_jobs(job_id, quiet=True)[job_id]
        reasons.add(job["Reason"])
        if "PartitionNodeLimit" in reasons or job["JobState"] == "COMPLETED":
            break

    assert (
        "PartitionNodeLimit" not in reasons
    ), f"Job within MaxNodes should never be limited, saw reasons: {reasons}"


@pytest.mark.skipif(
    atf.get_version() < (26, 5, 3),
    reason="Ticket 25443: the job pends with the undocumented PartitionConfig, and then with no reason at all, before 26.05.3",
)
def test_job_exceeds_max_nodes_pends(no_enforce_part_limits, max_nodes_partition):
    """Verify an over-limit job pends until MaxNodes is raised to allow it"""

    job_id = atf.submit_job_sbatch(
        f"-p {PARTITION_NAME} -n2 --ntasks-per-node=1 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    limited = atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="PartitionNodeLimit"
    )
    reason = atf.get_job_parameter(job_id, "Reason", quiet=True)
    assert limited, (
        f"squeue.1 documents PartitionNodeLimit when a job's node count is "
        f"outside its partition's limits, but the job pends with reason {reason}"
    )

    # Deliberately shorter than the default: this asserts the job never
    # starts, so the wait always runs out and a longer one only adds delay
    started = atf.wait_for_job_state(job_id, "RUNNING", timeout=10, xfail=True)
    assert not started, "Job needing more nodes than MaxNodes should not start"

    atf.run_command(
        f"scontrol update PartitionName={PARTITION_NAME} MaxNodes={RAISED_MAX_NODES}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)


@pytest.mark.parametrize(
    "submit_flags, update_fields",
    [
        ("-n2", "NumTasks=8 TasksPerNode=4"),
        pytest.param(
            "-n8",
            "TasksPerNode=4",
            marks=pytest.mark.skipif(
                atf.get_version() < (26, 5, 3),
                reason="Ticket 25443: a TasksPerNode update drops the job to 4 of its 8 requested tasks before 26.05.3",
            ),
        ),
        ("-n2 --ntasks-per-node=4", "NumTasks=8"),
    ],
)
def test_update_rederives_max_nodes(submit_flags, update_fields, update_partition):
    """Verify max_nodes is rederived from an updated task count"""

    job_id = atf.submit_job_sbatch(
        f"--hold -p {UPDATE_PARTITION_NAME} {submit_flags} -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    atf.run_command(
        f"scontrol update jobid={job_id} {update_fields}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(f"scontrol release {job_id}", fatal=True)

    completed = atf.wait_for_job_state(job_id, "COMPLETED")
    state = atf.get_job_parameter(job_id, "JobState", quiet=True)
    reason = atf.get_job_parameter(job_id, "Reason", quiet=True)

    assert (
        completed
    ), f"Job needs {UPDATE_MAX_NODES} nodes: state={state} reason={reason}"

    num_tasks = atf.get_job_parameter(job_id, "NumTasks")
    assert num_tasks == UPDATE_TASKS, (
        f"Job asked for {UPDATE_TASKS} tasks but was granted {num_tasks}. "
        f"scontrol.1 says TasksPerNode changes the requested tasks per node, "
        f"not the job's task count"
    )

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(node_list) == UPDATE_MAX_NODES, (
        f"Job should allocate ROUNDUP({UPDATE_TASKS}, 4)={UPDATE_MAX_NODES} node(s), "
        f"got {len(node_list)}: {node_list}"
    )


def test_update_tasks_keeps_explicit_max_nodes(update_partition):
    """Verify an explicit node count is not rederived from an updated task count"""

    job_id = atf.submit_job_sbatch(
        f"--hold -p {UPDATE_PARTITION_NAME} -N1 -n2 -J {test_name}"
        ' -t 1 --wrap "hostname" -o /dev/null',
        fatal=True,
    )
    atf.run_command(
        f"scontrol update jobid={job_id} NumTasks=8",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    node_list = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(node_list) == EXPLICIT_MAX_NODES, (
        f"Job should stay on the {EXPLICIT_MAX_NODES} node(s) it asked for, "
        f"got {len(node_list)}: {node_list}"
    )
