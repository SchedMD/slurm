############################################################################
# Purpose: Test of Slurm functionality
#          Test scheduling of gres/gpu and gres/mps
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

mps_cnt = 100 * 2
job_mps = int(mps_cnt * 0.5)
step_mps = int(job_mps * 0.5)


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "mps")
    atf.require_tty(1)
    atf.require_config_parameter(
        "Name",
        {"gpu": {"File": "/dev/tty[0-1]"}, "mps": {"Count": f"{mps_cnt}"}},
        source="gres",
    )
    atf.require_nodes(2, [("Gres", f"gpu:2,mps:{mps_cnt}"), ("CPUs", 6)])
    atf.require_config_parameter("FrontendName", None)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def mps_nodes():
    return atf.run_job_nodes(f"--gres=mps:{job_mps} -N2 hostname")


# Makes a commonly re-occuring batch script for file_in1 to allow for independent function tests
@pytest.fixture(scope="function")
def file_in_1a():
    file_in1 = atf.module_tmp_path / "input1"
    atf.make_bash_script(
        file_in1,
        f"""
    echo HOST:$SLURMD_NODENAME
    echo CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES
    echo CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    sleep 5
    """,
    )
    return file_in1


# Makes a commonly re-occuring batch script for file_in2 to allow for independent function tests
@pytest.fixture(scope="function")
def file_in_2a(file_in_1a):
    file_in2 = atf.module_tmp_path / "input2"
    atf.make_bash_script(
        file_in2,
        f"""
    srun --mem=0 --overlap --gres=mps:{job_mps}  {file_in_1a} &
    wait
    date
    srun --mem=0 --overlap --gres=mps:{step_mps} {file_in_1a} &
    srun --mem=0 --overlap --gres=mps:{step_mps} {file_in_1a} &
    wait
    date
    """,
    )
    return file_in2


def test_environment_vars(mps_nodes):
    """Simple MPS request, check environment variables"""

    file_in1 = atf.module_tmp_path / "input1"

    atf.make_bash_script(
        file_in1,
        f"""
    echo HOST:$SLURMD_NODENAME
    echo CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES
    echo CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    """,
    )

    results = atf.run_job(f"--gres=mps:{job_mps} -w {mps_nodes[0]} -n1 -t1 {file_in1}")
    assert results["exit_code"] == 0, "Job failed"
    assert (
        re.search(rf"HOST:{mps_nodes[0]}", results["stdout"]) is not None
    ), "HOST environmental variable not correct value"
    assert (
        match := re.search(r"CUDA_VISIBLE_DEVICES:(\d+)", results["stdout"])
    ) is not None and int(match.group(1)) == 0, "CUDA_VISIBLE_DEVICES != 0"
    assert (
        re.search(rf"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:{job_mps}", results["stdout"])
        is not None
    ), "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE environmental variable not correct value"


def test_two_parallel_consumption_sbatch(mps_nodes, file_in_2a):
    """Run two steps in parallel to consume gres/mps using sbatch"""

    file_out1 = atf.module_tmp_path / "output1"

    job_id = atf.submit_job_sbatch(
        f"--gres=mps:{job_mps} -w {mps_nodes[0]} -n1 -t1 -o {file_out1} {file_in_2a}"
    )
    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, "DONE", timeout=30, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")

    assert file_output is not None, "No output file"
    assert (
        len(re.findall(r"HOST:\w+", file_output)) == 3
    ), "HOST not found 3 times, once per job step, in output file"
    assert (
        re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is not None
    ), "CUDA_VISIBLE_DEVICES not found in output file"
    match = re.findall(r"(?s)CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:(\d+)", file_output)
    assert len(match) == 3, "Bad CUDA information about job (match != 3)"
    assert (
        sum(map(int, match)) == job_mps + step_mps * 2
    ), f"Bad CUDA percentage information about job (sum(map(int, match)) != {job_mps + step_mps * 2})"


def test_two_parallel_consumption_salloc(mps_nodes, file_in_2a):
    """Run two steps in parallel to consume gres/mps using salloc"""

    output = atf.run_command_output(
        f"salloc --gres=mps:{job_mps} -w {mps_nodes[0]} -n1 -t1 {file_in_2a}",
        fatal=True,
    )

    assert (
        len(re.findall(r"HOST:\w+", output)) == 3
    ), "HOST not found 3 times, once per job step, in output file"
    assert (
        re.search(r"CUDA_VISIBLE_DEVICES:\d+", output) is not None
    ), "CUDA_VISIBLE_DEVICES not found in output"
    match = re.findall(r"(?s)CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:(\d+)", output)
    assert len(match) == 3, "Bad CUDA information about job (match != 3)"
    assert (
        sum(map(int, match)) == job_mps + step_mps * 2
    ), f"Bad CUDA percentage information about job ({sum(map(int, match))} != {job_mps + step_mps * 2})"


def test_three_parallel_consumption_sbatch(mps_nodes, file_in_1a):
    """Run three steps in parallel to make sure steps get delayed as needed to avoid oversubscribing consumed MPS resources"""

    file_in2 = atf.module_tmp_path / "input2"
    file_out1 = atf.module_tmp_path / "output1"

    # Using -c6 for the job and -c2 for the steps to avoid issues if nodes have
    # HT. As Slurm allocates by Cores, with HT steps will allocate 2 CPUs even
    # if only 1 is requested, so the 3 steps won't run in parallel due lack of
    # CPUs instead of lack of MPS (that it's what we want to test).
    atf.make_bash_script(
        file_in2,
        f"""
    srun -vv --mem=0 -c2 --exact --gres=mps:{step_mps} {file_in_1a} &
    srun -vv --mem=0 -c2 --exact --gres=mps:{step_mps} {file_in_1a} &
    srun -vv --mem=0 -c2 --exact --gres=mps:{step_mps} {file_in_1a} &
    wait
    date
    """,
    )

    job_id = atf.submit_job_sbatch(
        f"--gres=mps:{job_mps} -w {mps_nodes[0]} -c6 -n1 -t1 -o {file_out1} {file_in2}"
    )

    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, "DONE", timeout=40, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")
    assert file_output is not None, "No output file"
    assert (
        len(re.findall(r"HOST:\w+", file_output)) == 3
    ), "HOST not found 3 times, once per job, in output file"
    assert (
        re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is not None
    ), "CUDA_VISIBLE_DEVICES not found in output file"
    match = re.findall(r"(?s)CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:(\d+)", file_output)
    assert len(match) == 3, "Bad CUDA information about job (match != 3)"
    assert (
        sum(map(int, match)) == step_mps * 3
    ), f"Bad CUDA percentage information about job ({sum(map(int, match))} != {step_mps * 3})"
    assert atf.check_steps_delayed(
        job_id, file_output, 1
    ), "Failed to delay step for sufficient MPS resources (match != 1)"


def test_consume_more_gresMps_than_allocated(mps_nodes, file_in_1a):
    """Run step to try to consume more gres/mps than allocated to the job"""

    file_in2 = atf.module_tmp_path / "input2"
    file_out1 = atf.module_tmp_path / "output1"
    job_mps2 = int(mps_cnt / 2)
    step_mps2 = job_mps2 + 1

    atf.make_bash_script(
        file_in2,
        f"""
    srun --mem=0 --overlap --gres=mps:{step_mps2} {file_in_1a}
    """,
    )

    job_id = atf.submit_job_sbatch(
        f"--gres=mps:{job_mps2} -w {mps_nodes[0]} -n1 -t1 -o {file_out1} {file_in2}"
    )

    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, "DONE", timeout=20, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")
    assert file_output is not None, "No output file"
    assert (
        re.search(r"Unable to create step", file_output) is not None
    ), "Did not give expected 'Unable to create step' output in file"
    assert (
        re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is None
    ), "Failed to reject bad step (match != 1)"


def test_run_multi_node_job(mps_nodes, file_in_1a):
    """Run multi-node job"""

    job_mps2 = int(mps_cnt / 2)
    node_cnt = len(mps_nodes)
    nodes_str = ",".join(mps_nodes)

    results = atf.run_job(
        f"--gres=mps:{job_mps2} -N{node_cnt} -w {nodes_str} -t1 {file_in_1a}"
    )

    assert results["exit_code"] == 0, "Job failed"
    host_match = re.findall(r"(?s)HOST:(\w+)", results["stdout"])
    assert len(host_match) is not None, "HOST not found"
    assert len(host_match) == len(
        mps_nodes
    ), f"Failed to get data from all nodes ({len(host_match)} != {len(mps_nodes)})"
    assert (
        host_match[0] != host_match[1]
    ), f"Two tasks ran on same node {host_match.group(0)}"


def test_gresGPU_gresMPS_GPU_sharing(mps_nodes):
    """Make sure that gres/gpu and gres/mps jobs either do not share the same GPU or run at different times"""

    file_in1 = atf.module_tmp_path / "input1"
    file_in2 = atf.module_tmp_path / "input2"
    file_out1 = atf.module_tmp_path / "output1"
    file_out2 = atf.module_tmp_path / "output2"
    job_mps2 = int(job_mps / 2)

    atf.make_bash_script(
        file_in1,
        f"""
    echo HOST:$SLURMD_NODENAME CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    scontrol -dd show job $SLURM_JOB_ID
    sbatch --gres=mps:{job_mps2} -w $SLURMD_NODENAME -n1 -t1 -o {file_out2} -J test_job {file_in2}
    sleep 30
    """,
    )

    atf.make_bash_script(
        file_in2,
        f"""
    echo HOST:$SLURMD_NODENAME CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    scontrol -dd show job $SLURM_JOB_ID
    squeue --name=test_job --noheader --state=r --format=\"jobid=%i state=%T\"
    """,
    )

    job_id = atf.submit_job_sbatch(
        f"--gres=gpu:1 -w {mps_nodes[0]} -n1 -t1 -o {file_out1} -J 'test_job' {file_in1}"
    )

    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, "DONE", timeout=60, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")
    assert file_output is not None, "No output file"
    assert (
        re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is not None
    ), "CUDA_VISIBLE_DEVICES not found in output file"
    assert (
        re.search(r"gpu:\d+\(IDX:\d+\)", file_output) is not None
    ), "GPU device index not found in output file"

    job_id2 = int(re.search(r"Submitted batch job (\d+)", file_output).group(1))

    assert job_id2 != 0, "Failed to submit second job"
    atf.wait_for_job_state(job_id2, "DONE", fatal=True)
    atf.wait_for_file(file_out2)
    file_output2 = atf.run_command_output(f"cat {file_out2}")
    assert file_output2 is not None, "No job2 output file"
    assert (
        re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output2) is not None
    ), "CUDA_VISIBLE_DEVICES not found in output2 file"

    assert (
        re.search(rf"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:{job_mps2}", file_output2)
        is not None
    ), f"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:{job_mps2} not found in output2 file"
    assert (
        re.search(rf"mps:{job_mps2}\(0/100,{job_mps2}/100\)", file_output2) is not None
    ), "Shared mps distribution across GPU devices not found in output2 file"

    assert (
        re.search(rf"jobid={job_id2} state=RUNNING", file_output2) is not None
        and re.search(rf"jobid={job_id} state=RUNNING", file_output2) is not None
    ), "Both jobs should be running at the same time"
