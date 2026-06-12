############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import os
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
    atf.require_nodes(node_count, [("CPUs", 4), ("Gres", "gpu:2")])

    # Require 8 tty because one test requests 8 "GPU"s (4 GPUS each for 2 nodes)
    for tty_num in range(node_count * 2):
        atf.require_tty(tty_num)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": f"/dev/tty[0-{node_count * 2 - 1}]"}}, source="gres"
    )

    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cleanup_state(setup):
    yield
    if os.path.exists("output.txt"):
        os.unlink("output.txt")


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


def test_step_exclusivity_default_exact():
    """
    User can run a job specifying --exclusive and get a whole node allocation
    with all GRES but with the exact behavior slicing up the CPUs and GPUs
    exactly

    Expected Result:
    0: Cpus_allowed_list:	0
    1: Cpus_allowed_list:	1
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """
    output = atf.run_job_output(
        '--exclusive --gpus-per-task=1 -n 2 -N 1 --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )
    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 1
    assert len(parse_cpurange(task_cpus["1"])) == 1
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None


@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 5),
    reason="Ticket 24115: The srun option --exclusive=allocate was added in 26.05+",
)
def test_step_exclusivity_allocation_only():
    """
    User can run a job specifying --exclusive=allocation and get a whole node
    allocation with all GRES but with all CPUs per task

    Expected Result:
    0: Cpus_allowed_list:	0-3
    1: Cpus_allowed_list:	0-3
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """
    output = atf.run_job_output(
        '--exclusive=allocation --gpus-per-task=1 -n 2 -N 1 --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )
    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 4
    assert len(parse_cpurange(task_cpus["1"])) == 4
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None


@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 5),
    reason="Ticket 24115: The srun option --exclusive=allocate was added in 26.05+",
)
def test_exclusive_exact_forward():
    """
    User can run a job specifying --exclusive=allocation --exact and get a
    whole node allocation with all GRES and retain exact behavior

    Expected Result:
    0: Cpus_allowed_list:	0
    1: Cpus_allowed_list:	1
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """
    output = atf.run_job_output(
        '--exclusive=allocation --exact --gpus-per-task=1 -n 2 -N 1 --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )
    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 1
    assert len(parse_cpurange(task_cpus["1"])) == 1
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None


@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 5),
    reason="Ticket 24115: The srun option --exclusive=allocate was added in 26.05+",
)
def test_exclusive_exact_reverse():
    """
    User can run a job specifying --exact --exclusive=allocation and get a
    whole node allocation with all GRES and retain exact behavior

    Expected Result:
    0: Cpus_allowed_list:	0
    1: Cpus_allowed_list:	1
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """
    output = atf.run_job_output(
        '--exact --exclusive=allocation --gpus-per-task=1 -n 2 -N 1 --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )
    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 1
    assert len(parse_cpurange(task_cpus["1"])) == 1
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None


@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 5),
    reason="Ticket 24115: The srun option --exclusive=allocate was added in 26.05+",
)
def test_sbatch_normal_with_exclusive_allocation():
    """
    User can submit a batch job specifying --exclusive=allocation and get a
    whole node allocation with all GRES and all CPUs by default (no exact)

    This test is to ensure the use of the option does not break standard
    behavior.  The --exclusive=allocation option is not intended to affect
    sbatch in any way.

    Expected Result:
    0: Cpus_allowed_list:	0-3
    1: Cpus_allowed_list:	0-3
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """

    output_file = "output.txt"

    job_id = atf.submit_job_sbatch(
        f"--exclusive=allocation --gpus-per-task=1 -n 2 -N 1 --output={output_file} --wrap='srun --label /bin/bash -c \"grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES\"'"
    )
    assert job_id != 0
    assert atf.wait_for_job_state(job_id, "DONE")

    with open(output_file, "r") as rfp:
        output = rfp.read().strip()

    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 4
    assert len(parse_cpurange(task_cpus["1"])) == 4
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None


@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 5),
    reason="Ticket 24115: The srun option --exclusive=allocate was added in 26.05+",
)
def test_salloc_normal_with_exclusive_allocation():
    """
    User can submit an interactive salloc job specifying --exclusive=allocation
    and get a whole node allocation with all GRES and all CPUs by default
    (no exact)

    This test is to ensure the use of the option does not break standard
    behavior.  The --excluive=allocation option is not intended to affect
    salloc in any way.

    Expected Result:
    0: Cpus_allowed_list:	0-3
    1: Cpus_allowed_list:	0-3
    0: CUDA_VISIBLE_DEVICES=0
    1: CUDA_VISIBLE_DEVICES=1
    """

    output_file = "output.txt"

    job_id = atf.submit_job_salloc(
        f'--exclusive=allocation --gpus-per-task=1 -n 2 -N 1 srun --output={output_file} --label /bin/bash -c "grep Cpus_allowed_list /proc/self/status && env | grep CUDA_VISIBLE_DEVICES"'
    )
    assert job_id != 0
    assert atf.wait_for_job_state(job_id, "DONE")

    with open(output_file, "r") as rfp:
        output = rfp.read().strip()

    task_cpus = get_task_cpus(output)
    assert len(parse_cpurange(task_cpus["0"])) == 4
    assert len(parse_cpurange(task_cpus["1"])) == 4
    assert re.search(r"0: CUDA_VISIBLE_DEVICES=\d", output) is not None
    assert re.search(r"1: CUDA_VISIBLE_DEVICES=\d", output) is not None
