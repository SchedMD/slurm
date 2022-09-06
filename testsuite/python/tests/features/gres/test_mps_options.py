############################################################################
# Purpose: Test of Slurm functionality
#          Test scheduling of gres/gpu and gres/mps
#
# Requires: SelectType=select/cons_tres
#           GresTypes=gpu,mps
#           tty0
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

mps_cnt = 100
job_mps = int(mps_cnt * .5)
step_mps = int(job_mps * .5)


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('SelectType', 'select/cons_tres')
    atf.require_config_parameter_includes('GresTypes', 'gpu')
    atf.require_config_parameter_includes('GresTypes', 'mps')
    atf.require_tty(0)
    atf.require_config_parameter('Name', {'gpu': {'File': '/dev/tty0'}, 'mps': {'Count': 100}}, source='gres')
    atf.require_nodes(2, [('Gres', f"gpu:1,mps:{mps_cnt}"), ('CPUs', 6)])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def mps_nodes():
    return atf.run_job_nodes(f"--gres=mps:{job_mps} -N2 hostname")


# Makes a commonly re-occuring batch script for file_in1 to allow for independent function tests
@pytest.fixture(scope="function")
def file_in_1a():
    file_in1 = atf.module_tmp_path / "input1"
    atf.make_bash_script(file_in1, f"""
    echo HOST:$SLURMD_NODENAME
    echo CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES
    echo CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    sleep 5
    """)
    return file_in1


# Makes a commonly re-occuring batch script for file_in2 to allow for independent function tests
@pytest.fixture(scope="function")
def file_in_2a(file_in_1a):
    file_in2 = atf.module_tmp_path / "input2"
    atf.make_bash_script(file_in2, f"""
    srun --mem=0 --overlap --gres=mps:{job_mps}  {file_in_1a} &
    wait
    date
    srun --mem=0 --overlap --gres=mps:{step_mps} {file_in_1a} &
    srun --mem=0 --overlap --gres=mps:{step_mps} {file_in_1a} &
    wait
    date
    """)
    return file_in2


def test_environment_vars(mps_nodes):
    """Simple MPS request, check environment variables"""

    file_in1 = atf.module_tmp_path / "input1"

    atf.make_bash_script(file_in1, f"""
    echo HOST:$SLURMD_NODENAME
    echo CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES
    echo CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    """)

    results  = atf.run_job(f"--gres=mps:{job_mps} -w {mps_nodes[0]} -n1 -t1 {file_in1}")
    assert results['exit_code'] == 0, "Job failed"
    assert re.search(r"HOST:\w+", results['stdout']) is not None, "HOST not found"
    assert (match := re.search(r"CUDA_VISIBLE_DEVICES:(\d+)", results['stdout'])) is not None and int(match.group(1)) == 0, "CUDA_VISIBLE_DEVICES != 0"
    assert re.search(r"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:(\d)+", results['stdout']) is not None, "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE not found"


def test_two_parallel_consumption_sbatch(mps_nodes, file_in_2a):
    """Run two steps in parallel to consume gres/mps using sbatch"""

    file_out1 = atf.module_tmp_path / "output1"

    job_id = atf.submit_job(f"--gres=mps:{job_mps} -w {mps_nodes[0]} -n1 -t1 -o {file_out1} {file_in_2a}")
    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, 'DONE', timeout=15, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")

    assert file_output is not None, "No output file"
    assert re.search(r"HOST:\w+", file_output) is not None, "HOST not found in output file"
    assert re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is not None, "CUDA_VISIBLE_DEVICES not found in output file"
    match = re.findall(r"(?s)CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:(\d+)", file_output)
    assert len(match) == 3, "Bad CUDA information about job (match != 3)"
    assert sum(map(int, match)) == mps_cnt, f"Bad CUDA percentage information about job (total percent != {mps_cnt})"


def test_two_parallel_consumption_salloc(mps_nodes,file_in_2a):
    """Run two steps in parallel to consume gres/mps using salloc"""

    output = atf.run_command_output(f"salloc --gres=mps:{job_mps} -w {mps_nodes[0]} -n1 -t1 {file_in_2a}", fatal=True)

    assert re.search(r"HOST:\w+", output) is not None, "HOST not found in output"
    assert re.search(r"CUDA_VISIBLE_DEVICES:\d+", output) is not None, "CUDA_VISIBLE_DEVICES not found in output"
    match = re.findall(r"(?s)CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:(\d+)", output)
    assert len(match) == 3, "Bad CUDA information about job (match != 3)"
    assert sum(map(int, match)) == mps_cnt, f"Bad CUDA percentage information about job (total percent != {mps_cnt})"


def test_three_parallel_consumption_sbatch(mps_nodes, file_in_1a):
    """Run three steps in parallel to make sure steps get delay as needed to avoid oversubscribing consumed MPS resources"""

    file_in2    = atf.module_tmp_path / "input2"
    file_out1   = atf.module_tmp_path / "output1"

    # Using -c6 for the job and -c2 for the steps to avoid issues if nodes have
    # HT. As Slurm allocates by Cores, with HT steps will allocate 2 CPUs even
    # if only 1 is requested, so the 3 steps won't run in parallel due lack of
    # CPUs instead of lack of MPS (that it's what we want to test).
    atf.make_bash_script(file_in2, f"""
    srun --mem=0 -c2 --exact --gres=mps:{step_mps} {file_in_1a} &
    srun --mem=0 -c2 --exact --gres=mps:{step_mps} {file_in_1a} &
    srun --mem=0 -c2 --exact --gres=mps:{step_mps} {file_in_1a} &
    wait
    date
    """)

    job_id = atf.submit_job(f"--gres=mps:{job_mps} -w {mps_nodes[0]} -c6 -n1 -t1 -o {file_out1} {file_in2}")

    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, 'DONE', timeout=20, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")
    assert file_output is not None, "No output file"
    assert re.search(r"HOST:\w+", file_output) is not None, "HOST not found in output file"
    assert re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is not None, "CUDA_VISIBLE_DEVICES not found in output file"
    match = re.findall(r"(?s)CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:(\d+)", file_output)
    assert len(match) == 3, "Bad CUDA information about job (match != 3)"
    assert sum(map(int, match)) == 75, f"Bad CUDA percentage information about job (total percent != 75)"
    assert re.search(r"step creation temporarily disabled", file_output) is not None, "Failed to delay step for sufficient MPS resources (match != 1)"


def test_consume_more_gresMps_than_allocated(mps_nodes, file_in_1a):
    """Run step to try to consume more gres/mps than allocated to the job"""

    file_in2 = atf.module_tmp_path / "input2"
    file_out1 = atf.module_tmp_path / "output1"
    job_mps2 = int(mps_cnt / 2)
    step_mps2 = job_mps2 + 1

    atf.make_bash_script(file_in2, f"""
    srun --mem=0 --overlap --gres=mps:{step_mps2} {file_in_1a}
    """)

    job_id = atf.submit_job(f"--gres=mps:{job_mps2} -w {mps_nodes[0]} -n1 -t1 -o {file_out1} {file_in2}")

    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, 'DONE', timeout=20, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")
    assert file_output is not None, "No output file"
    assert re.search(r"Unable to create step", file_output) is not None, "Did not give expected 'Unable to create step' output in file"
    assert re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is None, "Failed to reject bad step (match != 1)"


def test_run_multi_node_job(mps_nodes, file_in_1a):
    """Run multi-node job"""

    job_mps2 = int(mps_cnt / 2)
    node_cnt = len(mps_nodes)
    nodes_str = ','.join(map(str, mps_nodes))

    results = atf.run_job(f"--gres=mps:{job_mps2} -N{node_cnt} -w {nodes_str} -t1 {file_in_1a}")

    assert results['exit_code'] == 0, "Job failed"
    host_match = re.findall(r"(?s)HOST:(\w+)", results['stdout'])
    assert len(host_match) is not None, "HOST not found"
    assert len(host_match) > 1, "Failed to get data from multiple nodes (match < 2)"
    assert host_match[0] != host_match[1], f"Two tasks ran on same node {host_match.group(0)}"

    if atf.get_config_parameter('frontendname')    != "MISSING":
        atf.log_debug("Duplicate SLURMD_HOSTNAME in front-end mode as expected")


def test_gresGPU_gresMPS_GPU_sharing(mps_nodes):
    """Make sure that gres/gpu and gres/mps jobs either do not share the same GPU or run at different times"""

    if atf.get_config_parameter('frontendname') != None:
        atf.log_debug(f"par: {atf.get_config_parameter('frontendname')}")
        fname  = atf.get_config_parameter('frontendname')
        pytest.skip(f"SKIP: Subtest 7 doesn't work with front_end ({fname})")

    file_in1 = atf.module_tmp_path / "input1"
    file_in2 = atf.module_tmp_path / "input2"
    file_out1 = atf.module_tmp_path / "output1"
    file_out2 = atf.module_tmp_path / "output2"
    job_mps2 = int(mps_cnt / 2)

    atf.make_bash_script(file_in1, f"""
    echo HOST:$SLURMD_NODENAME CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    scontrol -dd show job $SLURM_JOB_ID
    sbatch --gres=mps:{job_mps2} -w $SLURMD_NODENAME -n1 -t1 -o {file_out2} -J test_job {file_in2}
    sleep 30
    """)

    atf.make_bash_script(file_in2, f"""
    echo HOST:$SLURMD_NODENAME CUDA_VISIBLE_DEVICES:$CUDA_VISIBLE_DEVICES CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE
    scontrol -dd show job $SLURM_JOB_ID
    squeue --name=test_job --noheader --state=r --format=\"jobid=%i state=%T\"
    """)

    job_id = atf.submit_job(f"--gres=gpu:1 -w {mps_nodes[0]} -n1 -t1 -o {file_out1} -J 'test_job' {file_in1}")

    assert job_id != 0, "Job failed to submit"
    atf.wait_for_job_state(job_id, 'DONE', timeout=35, fatal=True)
    atf.wait_for_file(file_out1)
    file_output = atf.run_command_output(f"cat {file_out1}")
    assert file_output is not None, "No output file"
    assert re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output) is not None, "CUDA_VISIBLE_DEVICES not found in output file"
    assert re.search(r"gpu:\d+\(IDX:\d+\)", file_output) is not None, "GPU device index not found in output file"

    dev1, dev2 = -1, -1
    dev1 = int(re.search(r"gpu:\d+\(IDX:(\d+)\)", file_output).group(1) )
    job_id2 = int(re.search(r"Submitted batch job (\d+)", file_output).group(1))

    assert job_id2 != 0, "Failed to submit second job"
    atf.wait_for_job_state(job_id2, 'DONE', fatal=True)
    atf.wait_for_file(file_out2)
    file_output2 = atf.run_command_output(f"cat {file_out2}")
    assert file_output2 is not None, "No job2 output file"
    assert re.search(r"CUDA_VISIBLE_DEVICES:\d+", file_output2) is not None, "CUDA_VISIBLE_DEVICES not found in output2 file"

    dev2 = int(re.search(r"CUDA_VISIBLE_DEVICES:(\d+)", file_output2).group(1))

    assert re.search(r"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE:\d+", file_output2) is not None, "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE not found in output2 file"
    assert re.search(r"mps:\d+\(IDX:\d+\)", file_output2) is not None, "GPU device index not found in output2 file"

    dev1 = int(re.search(r"mps:\d+\(IDX:(\d+)\)", file_output2).group(1))
    running = 0

    if re.search(rf"jobid={job_id} state=RUNNING", file_output2) is not None:
        running = 1

    if dev1 == dev2:
        if running == 0:
            atf.log_debug(f"The jobs are using the same GPU {dev1} and running at different times, which is fine")
        else:
            pytest.fail(f"The jobs are using the same GPU {dev1} and running at the same time")
    else:
        atf.log_debug(f"The jobs are using different GPUs ({dev1} != {dev2}), which is fine")
