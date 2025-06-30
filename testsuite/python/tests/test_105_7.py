############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import json
import pytest
import re

file_in1 = "input1"
file_in2 = "input2"
file_out = "output"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Setup for the test module"""
    atf.require_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": "/dev/tty[0-1]"}}, source="gres"
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        2, [("CPUs", 3), ("Sockets", 3), ("Gres", "gpu:2"), ("RealMemory", 1)]
    )

    atf.require_slurm_running()


def check_accounted_gpus(
    job_id, job_gpus, step_gpus, req_gpus, have_gpu_types, batch_gpus
):
    """Validate that the job, batch step and step 0 of a job have the proper GPU counts using JSON.

    Args:
        job_id: Job ID to check
        job_gpus: Expected number of GPUs for the job
        step_gpus: Expected number of GPUs per step, None if no step to test
        req_gpus: Expected number of GPUs requested per node/task/socket
        have_gpu_types: Whether to look for GPU types in the accounting data
        batch_gpus: Expected number of GPUs for the batch step
    """

    def _gpus_from_tres_arrays(tres_arrays):
        """Helper to count GPU TRES from sacct JSON tres arrays."""
        gpu_count = 0
        for tres_array in tres_arrays:
            for tres_item in tres_array:
                if tres_item.get("type") == "gres" and tres_item.get("name") == "gpu":
                    gpu_count += tres_item.get("count", 0)
        return gpu_count

    if job_id == 0:
        return

    # Wait for job to show up in the DB
    atf.wait_for_job_accounted(job_id, fatal=True)

    # If needed, wait for step to show up as well
    if step_gpus is not None:
        atf.wait_for_step_accounted(job_id, 0, fatal=True)

    # Get accounting data using JSON
    output = atf.run_command_output(
        f"sacct --job={job_id} --json --start=now-15minutes",
        fatal=True,
    )
    data = json.loads(output)
    jobs = data.get("jobs", [])
    if len(jobs) != 1:
        pytest.fail(f"sacct reported wrong number jobs for {job_id}")

    job_data = jobs[0]

    # Check and count reported gpus on the step
    if step_gpus is not None:
        step_found = False
        for step in job_data.get("steps", []):
            step_id = step.get("step", {}).get("id", "")
            if step_id == f"{job_id}.0":
                step_found = True
                allocated_gpus = _gpus_from_tres_arrays(
                    [step.get("tres", {}).get("allocated", [])]
                )
                assert (
                    allocated_gpus == step_gpus
                ), f"Step GPUs reported by sacct should be {step_gpus}, found {allocated_gpus}"
                break

        if not step_found:
            pytest.fail(f"sacct did not report a record for step {job_id}.0")

    # Check and count reported batch gpus on the job
    batch_found = False
    for step in job_data.get("steps", []):
        step_id = step.get("step", {}).get("id", "")
        if step_id == f"{job_id}.batch":
            batch_found = True
            allocated_gpus = _gpus_from_tres_arrays(
                [step.get("tres", {}).get("allocated", [])]
            )
            assert (
                allocated_gpus == batch_gpus
            ), f"Batch GPUs reported by sacct should be {batch_gpus}, found {allocated_gpus}"
            break

    if not batch_found:
        pytest.fail(f"sacct did not report a record for job {job_id}.batch")

    # Check and count reported gpus on the job
    job_tres = job_data.get("tres", {})
    allocated_gpus = _gpus_from_tres_arrays([job_tres.get("allocated", [])])
    requested_gpus = _gpus_from_tres_arrays([job_tres.get("requested", [])])

    # Count how many times we found valid GPU TRES
    gpus_reported_count = 0

    # Check allocated GPUs (AllocTRES)
    assert (
        allocated_gpus == job_gpus
    ), f"Job allocated GPUs reported by sacct should be {job_gpus}, found {allocated_gpus}"
    gpus_reported_count += 1

    # Check requested GPUs (ReqTRES)
    assert (
        requested_gpus == job_gpus
    ), f"Job requested GPUs reported by sacct should be {job_gpus}, found {requested_gpus}"
    gpus_reported_count += 1

    assert (
        gpus_reported_count == 2
    ), f"sacct should report job GPUs 2 times (once in AllocTRES and once in ReqTRES), found {gpus_reported_count} times"


def check_allocated_gpus(job_id, target):
    """Validate the job has the proper GPU counts.

    Args:
        job_id: Job ID to check
        target: Expected GPU count
    """

    # Extract GPU count from allocated TRES
    tres_string = atf.get_job_parameter(job_id, "AllocTRES", "")
    if not tres_string:
        gpu_count = 0
    else:
        gpu_match = re.search(r"gres/gpu[=:](\d+)", tres_string)
        gpu_count = int(gpu_match.group(1)) if gpu_match else 0

    assert (
        gpu_count == target
    ), f"GPUs accounted should be {target}, but found {gpu_count}"


def get_batch_gpus():
    """Helper function to find batch_gpus from different outputs.

    Returns:
        Number of batch GPUs
    """
    batch_host = "unknown"

    output = atf.run_command_output(f"cat {file_out}", fatal=True)
    nodes_lines = re.findall(r"    Nodes=+.*", output, re.MULTILINE)

    if not nodes_lines:
        pytest.fail("No Nodes lines found in output file")

    node_line = nodes_lines[0]

    if len(nodes_lines) > 1:
        # Output type where nodes are split on 2 lines
        #  BatchHost=74dc179a_n1
        #  ...
        # >Nodes=74dc179a_n1 CPU_IDs=0-1 Mem=150 GRES=[[gpu:2]](IDX:0-1)<
        #  Nodes=74dc179a_n2 CPU_IDs=0-1 Mem=150 GRES=gpu:1(IDX:0)
        batch_host_match = re.search(r"BatchHost=(.*)", output)
        if batch_host_match:
            batch_host = batch_host_match.group(1)

        for line in nodes_lines:
            if batch_host in line:
                node_line = line
                break

    batch_gpus_match = re.search(r"gpu:(?:[^:( ]+:)?(\d+)", node_line)
    assert batch_gpus_match, "Unable to get batch_gpus"

    return int(batch_gpus_match.group(1))


@pytest.fixture(scope="function", autouse=True)
def clear_output_file():
    yield
    atf.run_command(f"rm -f {file_out}", quiet=True)


def test_gpus_per_node_job():
    """Test --gpus-per-node option by job."""
    atf.make_bash_script(
        file_in1,
        """
        scontrol -dd show job ${SLURM_JOBID}
        exit 0
    """,
    )

    nb_nodes = 2
    req_gpus = 2
    target = nb_nodes * req_gpus

    job_id = atf.submit_job_sbatch(
        f"--gpus-per-node={req_gpus} -N{nb_nodes} -t1 -o {file_out} -J test_gpus_per_node_job {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, target)
    check_accounted_gpus(job_id, target, None, req_gpus, False, batch_gpus)


def test_gpus_job():
    """Test --gpus option by job."""
    atf.make_bash_script(
        file_in1,
        """
        scontrol -dd show job ${SLURM_JOBID}
        exit 0
    """,
    )

    target = 2

    job_id = atf.submit_job_sbatch(
        f"--gpus={target} -N2 -t1 -o {file_out} -J test_gpus_job {file_in1}", fatal=True
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, target)
    check_accounted_gpus(job_id, target, None, target, False, batch_gpus)


def test_gpus_per_task_job():
    """Test --gpus-per-task option by job."""
    atf.make_bash_script(
        file_in1,
        """
        scontrol -dd show job ${SLURM_JOBID}
        exit 0
    """,
    )

    nb_tasks = 3
    req_gpus = 1

    job_id = atf.submit_job_sbatch(
        f"--gpus-per-task={req_gpus} -N2 -n{nb_tasks} -t1 -o {file_out} -J test_gpus_per_task_job {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, nb_tasks)
    check_accounted_gpus(job_id, nb_tasks, None, req_gpus, False, batch_gpus)


# TODO: Remove xfail once ticket 19605 is fixed.
@pytest.mark.xfail(reason="Ticket 19605. ReqTRES should not be > AllocTRES.")
def test_gpus_per_socket_job():
    """Test --gpus-per-socket option by job."""
    atf.make_bash_script(
        file_in1,
        """
        scontrol -dd show job ${SLURM_JOBID}
        exit 0
    """,
    )

    nb_nodes = 2
    nb_sockets = 2
    cpus_per_task = 1
    req_gpus = 1
    target = nb_nodes

    job_id = atf.submit_job_sbatch(
        f"--gpus-per-socket={req_gpus} -N{nb_nodes} --ntasks={nb_nodes} --sockets-per-node={nb_sockets} --cpus-per-task={cpus_per_task} -t1 -o {file_out} -J test_gpus_per_socket_job {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, target)
    check_accounted_gpus(job_id, target, None, req_gpus, False, batch_gpus)


def test_gpus_per_node_step():
    """Test --gpus-per-node option by step."""
    atf.make_bash_script(
        file_in1,
        f"""
        srun {file_in2}
        exit 0
    """,
    )

    atf.make_bash_script(
        file_in2,
        """
        if [ $SLURM_PROCID -eq 0 ]; then
            scontrol -dd show job ${SLURM_JOBID}
            scontrol show step ${SLURM_JOBID}.${SLURM_STEPID}
        fi
        exit 0
    """,
    )

    nb_nodes = 2
    req_gpus = 2
    target = nb_nodes * req_gpus

    job_id = atf.submit_job_sbatch(
        f"--gpus-per-node={req_gpus} -N{nb_nodes} -t1 -o {file_out} -J test_gpus_per_node_step {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, target)
    check_accounted_gpus(job_id, target, target, req_gpus, False, batch_gpus)


def test_gpus_step():
    """Test --gpus option by step."""
    atf.make_bash_script(
        file_in1,
        f"""
        srun {file_in2}
        exit 0
    """,
    )

    atf.make_bash_script(
        file_in2,
        """
        if [ $SLURM_PROCID -eq 0 ]; then
            scontrol -dd show job ${SLURM_JOBID}
            scontrol show step ${SLURM_JOBID}.${SLURM_STEPID}
        fi
        exit 0
    """,
    )

    nb_nodes = 2
    target = 2

    job_id = atf.submit_job_sbatch(
        f"--gpus={target} -N{nb_nodes} -t1 -o {file_out} -J test_gpus_step {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, target)
    check_accounted_gpus(job_id, target, target, target, False, batch_gpus)


def test_gpus_per_task_step():
    """Test --gpus-per-task option by step."""
    atf.make_bash_script(
        file_in1,
        f"""
        srun {file_in2}
        exit 0
    """,
    )

    atf.make_bash_script(
        file_in2,
        """
        if [ $SLURM_PROCID -eq 0 ]; then
            scontrol -dd show job ${SLURM_JOBID}
            scontrol show step ${SLURM_JOBID}.${SLURM_STEPID}
        fi
        exit 0
    """,
    )

    nb_nodes = 2
    nb_tasks = 3
    req_gpus = 1

    job_id = atf.submit_job_sbatch(
        f"--gpus-per-task={req_gpus} -N{nb_nodes} -n{nb_tasks} -t1 -o {file_out} -J test_gpus_per_task_step {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, nb_tasks)
    check_accounted_gpus(job_id, nb_tasks, nb_tasks, req_gpus, False, batch_gpus)


# TODO: Remove xfail once ticket 19605 is fixed.
@pytest.mark.xfail(reason="Ticket 19605. ReqTRES should not be > AllocTRES.")
def test_gpus_per_socket_step():
    """Test --gpus-per-socket option by step."""
    atf.make_bash_script(
        file_in1,
        f"""
        srun {file_in2}
        exit 0
    """,
    )

    atf.make_bash_script(
        file_in2,
        """
        if [ $SLURM_PROCID -eq 0 ]; then
            scontrol -dd show job ${SLURM_JOBID}
            scontrol show step ${SLURM_JOBID}.${SLURM_STEPID}
        fi
        exit 0
    """,
    )

    nb_nodes = 2
    nb_sockets = 2
    cpus_per_task = 1
    req_gpus = 1
    target = nb_nodes

    job_id = atf.submit_job_sbatch(
        f"--gpus-per-socket={req_gpus} -N{nb_nodes} --ntasks={nb_nodes} --sockets-per-node={nb_sockets} --cpus-per-task={cpus_per_task} -t1 -o {file_out} -J test_gpus_per_socket_step {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, target)
    check_accounted_gpus(job_id, target, target, req_gpus, False, batch_gpus)


def test_gpus_per_task_with_explicit_step():
    """Test --gpus-per-task option with explicit step node/tasks."""
    nb_nodes = 2
    step_nodes = 2
    job_tasks = 3
    step_tasks = 2
    req_gpus = 1

    atf.make_bash_script(
        file_in1,
        f"""
        srun -N{step_nodes} -n{step_tasks} {file_in2}
        exit 0
    """,
    )

    atf.make_bash_script(
        file_in2,
        """
        if [ $SLURM_PROCID -eq 0 ]; then
            scontrol -dd show job ${SLURM_JOBID}
            scontrol show step ${SLURM_JOBID}.${SLURM_STEPID}
        fi
        exit 0
    """,
    )

    job_id = atf.submit_job_sbatch(
        f"--gpus-per-task={req_gpus} -N{nb_nodes} -n{job_tasks} -t1 -o {file_out} -J test_gpus_per_task_with_explicit_step {file_in1}",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    batch_gpus = get_batch_gpus()

    check_allocated_gpus(job_id, job_tasks)
    check_accounted_gpus(job_id, job_tasks, step_tasks, req_gpus, False, batch_gpus)
