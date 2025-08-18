############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import logging
import json
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom gpu files and custom gres")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_accounting(True)
    atf.require_config_parameter("AccountingStorageTres", "gres/gpu")
    atf.require_config_parameter("GresTypes", "gpu")
    atf.require_nodes(
        2, [("Gres", "gpu:6 Sockets=2 CoresPerSocket=8 ThreadsPerCore=2")]
    )
    # GPUs need to point to existing files
    gpu_file = f"{str(atf.module_tmp_path)}/gpu"
    for i in range(0, 6):
        atf.run_command(f"touch {gpu_file}{i}")
    node_names = "node[1-2]"
    atf.require_config_parameter(
        "NodeName",
        f"""{node_names} Name=gpu Cores=0-7 File={gpu_file}[0-2]
NodeName={node_names} Name=gpu Cores=8-15 File={gpu_file}[3-5]""",
        source="gres",
    )
    atf.require_slurm_running()


# The tests for "no enforce binding" don't test for specific cpu or gpu indices, since we offer no guarantee which
# sockets will be selected. This only checks for the quantity of allocated tres.
no_enforce_binding_parameters = [
    (
        "case00 - 1task_2cpu_6gpu_per_task",
        "The job should have one core.",
        "--ntasks-per-node=1 -N1 -c2 --tres-per-task=gres/gpu:6",
        "cpu=2,node=1,billing=2,gres/gpu=6",
    ),
    (
        "case01 - 1task_2cpu_4gpu",
        "The job should have one core.",
        "--gres=gpu:4 -n1 -c2",
        "cpu=2,node=1,billing=2,gres/gpu=4",
    ),
    (
        "case02 - 4cpu_4gpu",
        "The job should have two cores.",
        "--gres=gpu:4 -c4",
        "cpu=4,node=1,billing=4,gres/gpu=4",
    ),
    (
        "case03 - 2tasks_4cpus_per_gpu",
        "The job should have 4 cpus.",
        "--ntasks-per-node=2 --cpus-per-gpu=4 --gres=gpu:1 -N1",
        "cpu=4,node=1,billing=4,gres/gpu=1",
    ),
]


@pytest.mark.parametrize(
    "test_id,description,job_args,exp_alloc_tres",
    no_enforce_binding_parameters,
    ids=[param[0] for param in no_enforce_binding_parameters],
)
def test_no_enforce_binding(test_id, description, job_args, exp_alloc_tres):
    job_str = f'{job_args} --wrap "exit 0"'
    logging.info(f"{description} [Job submission: sbatch {job_str}]")
    job_id = atf.submit_job_sbatch(job_str, fatal=True, quiet=False)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=60, quiet=False)
    job_dict = atf.get_job(job_id)
    assert job_dict["AllocTRES"] == exp_alloc_tres


# The enforce binding tests need to test for specific indices to verify core placement on sockets.
enforce_binding_parameters = [
    pytest.param(
        "case00 - 2tasks_3gpu_per_task",
        "The job should have one core on each socket.",
        "-n2 --tres-per-task=gres/gpu:3",
        "0-1,16-17",
        "gpu:6(IDX:0-5)",
        marks=pytest.mark.xfail(
            atf.get_version() < (24, 11, 1),
            reason="Ticket 21262: Fixed in 24.11.1",
        ),
    ),
    pytest.param(
        "case01 - 1task_3gpu_per_task",
        "The job should have one core on the same socket as the gpus.",
        "-n1 --tres-per-task=gres/gpu:3",
        "0-1",
        "gpu:3(IDX:0-2)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="MR !1713: Socket binding fixed in 25.11",
        ),
    ),
    pytest.param(
        "case02 - 2tasks_2cpu_3gpu_per_task",
        "The job should have one core on each socket.",
        "--ntasks-per-node=2 -N1 -c2 --tres-per-task=gres/gpu:3",
        "0-1,16-17",
        "gpu:6(IDX:0-5)",
        marks=pytest.mark.xfail(
            atf.get_version() < (24, 11, 1),
            reason="Ticket 21262: Fixed in 24.11.1",
        ),
    ),
    # Submit with --sockets-per-node=2 to make sure it still runs. This doesn't change the job
    # allocation. This tests a previous bug in which this job was rejected at submission time.
    pytest.param(
        "case03 - 2sockets_2tasks_2cpu_3gpu",
        "The job should have one core on each socket.",
        "--sockets-per-node=2 --ntasks-per-node=2 -N1 -c2 --tres-per-task=gres/gpu:3",
        "0-1,16-17",
        "gpu:6(IDX:0-5)",
        marks=pytest.mark.xfail(
            atf.get_version() < (24, 11, 1),
            reason="Ticket 21262: Fixed in 24.11.1",
        ),
    ),
    pytest.param(
        "case04 - 2tasks_2cpu_2gpu_per_task",
        "The job should have one core on each socket and 2 gpus on each socket.",
        "--ntasks-per-node=2 -N1 -c2 --tres-per-task=gres/gpu:2",
        "0-1,16-17",
        "gpu:4(IDX:0-1,3-4)",
        marks=pytest.mark.xfail(
            atf.get_version() < (24, 11, 1),
            reason="Ticket 21262: Fixed in 24.11.1",
        ),
    ),
    (
        "case05 - 1task_2cpu_3gpu",
        "The job should have one core on the same socket as the gpus.",
        "--gres=gpu:3 -n1 -c2",
        "0-1",
        "gpu:3(IDX:0-2)",
    ),
    pytest.param(
        "case06 - 1task_2cpu_4gpu",
        "The job should have one core on each socket.",
        "--gres=gpu:4 -n1 -c2",
        "0-1,16-17",
        "gpu:4(IDX:0-1,3-4)",
        marks=pytest.mark.xfail(
            atf.get_version() < (24, 11, 1),
            reason="Ticket 21262: Fixed in 24.11.1",
        ),
    ),
    (
        "case07 - 1task_4cpu_4gpu",
        "The job should have one core on each socket.",
        "--gres=gpu:4 -n1 -c4",
        "0-1,16-17",
        "gpu:4(IDX:0-1,3-4)",
    ),
    (
        "case08 - 2gpu_1cpu_per_gpu",
        "The job should have one core.",
        "--gpus=2 --cpus-per-gpu=1",
        "0-1",
        "gpu:2(IDX:0-1)",
    ),
    (
        "case09 - 4gpu_1cpu_per_gpu",
        "The job should have 2 cpus on each socket.",
        "--gpus=4 --cpus-per-gpu=1",
        "0-1,16-17",
        "gpu:4(IDX:0-1,3-4)",
    ),
    (
        "case10 - 2tasks_4cpu_per_gpu_1gpu",
        "The job should have 2 cores on one socket.",
        "--ntasks-per-node=2 --cpus-per-gpu=4 --gres=gpu:1 -N1",
        "0-3",
        "gpu:1(IDX:0)",
    ),
    (
        "case11 - 1task_3gpu_2cpu_per_gpu",
        "The job should have 3 cores on one socket.",
        "-N1 --gpus-per-task=3 -n1 --cpus-per-gpu=2",
        "0-5",
        "gpu:3(IDX:0-2)",
    ),
    # This job implicitly requests just one cpu. However, with enforce-binding, it needs one core
    # on each socket.
    pytest.param(
        "case12 - 1task_6gpu_implicit_cpu",
        "The job should have one core on each socket.",
        "-n1 --gpus=6 -N1",
        "0-1,16-17",
        "gpu:6(IDX:0-5)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="MR !1713: Socket binding fixed in 25.11",
        ),
    ),
]


@pytest.mark.parametrize(
    "test_id,description,job_args,cpu_ids,gpu_idx",
    enforce_binding_parameters,
    ids=[
        param.values[0] if hasattr(param, "values") else param[0]
        for param in enforce_binding_parameters
    ],
)
def test_enforce_binding(test_id, description, job_args, cpu_ids, gpu_idx):
    job_str = f'{job_args} --gres-flags=enforce-binding --wrap "sleep infinity"'
    logging.info(f"{description} [Job submission: sbatch {job_str}]")
    job_id = atf.submit_job_sbatch(job_str, fatal=True, quiet=False)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60, quiet=False)
    job_dict = atf.get_job(job_id)
    atf.cancel_all_jobs(quiet=True)
    assert job_dict["CPU_IDs"] == cpu_ids
    assert job_dict["GRES"] == gpu_idx


# Get output of scontrol --json show jobs <job_id>
def _get_job_json(job_id):
    jobs_dict = {}
    job = {}

    output = atf.run_command_output(
        f"scontrol --json show jobs {job_id}", fatal=True, quiet=True
    )
    jobs = json.loads(output)["jobs"]

    for job in jobs:
        jobs_dict[job["job_id"]] = job
    if job_id not in jobs_dict:
        pytest.fail(f"{job_id} was not found in the system")
    else:
        job = jobs_dict[job_id]
    return job


def _validate_job_allocation(job, exp_gres, exp_nodes, exp_cpu_count, exp_cores):
    job_allocation = job["job_resources"]["nodes"]["allocation"]
    gres_detail = job["gres_detail"]

    assert len(gres_detail) == len(exp_gres)
    for i in range(0, len(gres_detail)):
        assert gres_detail[i] == exp_gres[i]

    assert len(job_allocation) == len(exp_nodes)
    for i in range(0, len(job_allocation)):
        assert job_allocation[i]["name"] == exp_nodes[i]

    assert len(job_allocation) == len(exp_cpu_count)
    for i in range(0, len(job_allocation)):
        assert job_allocation[i]["cpus"]["count"] == exp_cpu_count[i]

    assert len(job_allocation) == len(exp_cores)
    # Each node
    for i in range(0, len(job_allocation)):
        # 2 sockets
        for s in range(0, 2):
            # 8 cores
            for c in range(0, 8):
                status = job_allocation[i]["sockets"][s]["cores"][c]["status"][0]
                # Convert core index on the socket to core index on the node
                node_core_inx = (s * 8) + c
                if node_core_inx in exp_cores[i]:
                    assert status == "ALLOCATED"
                else:
                    assert status == "UNALLOCATED"


multi_node_parameters = [
    (
        "case00 - 2nodes_2tasks_6gpu_total",
        "-N2 --gpus=6 -n2",
        ["gpu:5(IDX:0-4)", "gpu:1(IDX:0)"],
        ["node1", "node2"],
        [4, 4],
        [[0, 8], [0, 8]],
    ),
    (
        "case01 - 2nodes_2tasks_3gpu_per_task",
        "-N2 --gpus-per-task=3 -n2 --cpus-per-gpu=2",
        ["gpu:3(IDX:0-2)", "gpu:3(IDX:0-2)"],
        ["node1", "node2"],
        [6, 6],
        [[0, 1, 2], [0, 1, 2]],
    ),
]


@pytest.mark.xfail(
    atf.get_version() < (25, 11),
    reason="MR !1713: Multi-node binding fixed in 25.11",
)
@pytest.mark.parametrize(
    "test_id,job_args,exp_gres,exp_nodes,exp_cpu_count,exp_cores",
    multi_node_parameters,
    ids=[param[0] for param in multi_node_parameters],
)
def test_multi_node_enforce_binding(
    test_id, job_args, exp_gres, exp_nodes, exp_cpu_count, exp_cores
):
    job_str = f'{job_args} --gres-flags=enforce-binding --wrap "sleep infinity"'
    job_id = atf.submit_job_sbatch(job_str, fatal=True, quiet=False)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True, timeout=60, quiet=False)
    job = _get_job_json(job_id)

    # Do this for debugging purposes: so the test will log the output of scontrol show job
    atf.get_job(job_id)

    atf.cancel_all_jobs(quiet=True)
    _validate_job_allocation(job, exp_gres, exp_nodes, exp_cpu_count, exp_cores)
