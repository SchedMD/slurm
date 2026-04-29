############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re

import atf
import pytest

node_count = 1
cpu_allowed_list_regex = re.compile(r"([0-9]+):\s*Cpus_allowed_list:\s*([0-9\-,]+)")


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TaskPlugin", "cgroup,affinity")
    atf.require_config_parameter_includes(
        "LaunchParameters", "srun_exclusive_allocation"
    )
    atf.require_nodes(node_count, [("CPUs", 4), ("Gres", "gpu:2")])

    # Require 8 tty because one test requests 8 "GPU"s (4 GPUS each for 2 nodes)
    for tty_num in range(node_count * 2):
        atf.require_tty(tty_num)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": f"/dev/tty[0-{node_count * 2 - 1}]"}}, source="gres"
    )

    atf.require_slurm_running()


def parse_cpurange(s):
    result = []
    for block in s.split(","):
        if "-" in block:
            start, end = block.split("-")
            result.extend(range(int(start), int(end) + 1))
        else:
            result.append(int(block))
    return tuple(result)


def get_task_cpus(output):
    ret = {}
    for line in output.split("\n"):
        match = cpu_allowed_list_regex.search(line)
        if match:
            ret[match.groups()[0]] = match.groups()[1]
    return ret


@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 5),
    reason="Ticket 24115: LaunchParamters=srun_exclusive_allocation was added in 26.05+",
)
def test_srun_exclusive_allocation_launch_param():
    """
    User can run a job specifying --exclusive and get a whole node
    allocation with all GRES but with all CPUs per task, so long as
    slurm.conf LaunchParamters=srun_exclusive_allocation parameter
    is set

    Expected Result:
    0: Cpus_allowed_list:	0-3
    1: Cpus_allowed_list:	0-3
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """
    output = atf.run_job_output(
        '--exclusive --gpus-per-task=1 -n 2 -N 1 --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )

    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 4
    assert len(parse_cpurange(task_cpus["1"])) == 4
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None


def test_srun_exclusive_allocation_with_exact():
    """
    User can run a job specifying --exclusive --exact and get a whole node
    allocation with all GRES but specific CPUs set per task, even with
    LaunchParamters=srun_exclusive_allocation parameter set.

    Expected Result:
    0: Cpus_allowed_list:	0
    1: Cpus_allowed_list:	1
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """
    output = atf.run_job_output(
        '--exclusive --exact --gpus-per-task=1 -n 2 -N 1 --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )

    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 1
    assert len(parse_cpurange(task_cpus["1"])) == 1
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None


def test_srun_exclusive_allocation_with_exact_reverse():
    """
    User can run a job specifying --exact --exclusive and get a whole node
    allocation with all GRES but specific CPUs set per task, even if
    LaunchParamters=srun_exclusive_allocation parameter is set.

    Expected Result:
    0: Cpus_allowed_list:	0
    1: Cpus_allowed_list:	1
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1

    Moreover, doing this with `--exact` first ensures that `--exclusive` is not
    overwriting anything and making it so we get different behavior depending
    on order of the arguments.
    """
    output = atf.run_job_output(
        '--exact --exclusive --gpus-per-task=1 -n 2 -N 1 --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )

    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 1
    assert len(parse_cpurange(task_cpus["1"])) == 1
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None
