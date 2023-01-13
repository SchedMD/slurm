############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os
import re

total_cpus = 0
total_cores = 0
available_cores = 0


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("needs nodes with and specific number of Cores and CoreSpecCount")
    atf.require_accounting(modify=False)
    atf.require_config_parameter('SelectType', 'select/cons_res')
    atf.require_config_parameter('CoreSpecPlugin', 'core_spec/none')
    atf.require_config_parameter('TaskPlugin', 'task/cgroup')
    atf.require_config_parameter('AllowSpecResourcesUsage', '1')
    atf.require_nodes(2, [('Cores', 3)])
    atf.require_config_parameter('ConstrainCores', 'yes', source='cgroup')
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def node_names():
    global total_cpus, total_cores, available_cores

    # Grab our 2 nodes with 2 Cores each
    nodes = atf.run_job_nodes(f"--cpu-bind=core --ntasks-per-node=2 -N2 true")

    # Reserve 1 spec core on the first, 2 on the second
    for idx, node in enumerate(nodes):
        atf.set_node_parameter(node, 'CoreSpecCount', idx + 1)
        sockets = atf.get_node_parameter(node, 'Sockets')
        cores_per_socket = atf.get_node_parameter(node, 'CoresPerSocket')
        threads_per_core = atf.get_node_parameter(node, 'ThreadsPerCore')
        total_cores += sockets * cores_per_socket
        total_cpus += sockets * cores_per_socket * threads_per_core

    available_cores = total_cores - 3
    return (','.join(nodes))


@pytest.fixture(scope="function")
def teardown_jobs():
    yield
    atf.cancel_all_jobs(quiet=True)


def test_job_submit(node_names):
    """Verify a properly formed job submits with CoreSpecCount plugin enabled"""

    exit_code = atf.run_job_exit(f"-w {node_names} -N2 -n{available_cores} true")
    assert exit_code == 0


def test_job_denied(node_names):
    """Verify a malformed job is rejected with CoreSpecCount plugin enabled"""

    exit_code = atf.run_job_exit(f"-w {node_names} -N2 -n{total_cores} true", xfail=True)
    assert exit_code != 0


def test_node_state(node_names, teardown_jobs):
    """Verify that sinfo state is returned as 'alloc' when using all cpus except specialized cores"""

    job_id = atf.submit_job(f"-w {node_names} -n{available_cores} --wrap='srun sleep 60'")
    atf.wait_for_job_state(job_id, "RUNNING")

    assert len(re.findall("alloc", atf.run_command_output(f"sinfo -n {node_names} -h -N -o%t"))) == 2, "node states in sinfo should be both 'alloc'"

    atf.cancel_all_jobs(quiet=True)

    job_id = atf.submit_job(f"-w {node_names} -n2 --wrap='srun sleep 60'")
    atf.wait_for_job_state(job_id, "RUNNING")

    assert len(re.findall("alloc", atf.run_command_output(f"sinfo -n {node_names} -h -N -o%t"))) == 1, "one node state in sinfo should be 'alloc'"
    assert len(re.findall("mix", atf.run_command_output(f"sinfo -n {node_names} -h -N -o%t"))) == 1, "one node state in sinfo should be 'mix'"


def test_core_spec_override(node_names):
    """Verify that if you use the --core-spec option with less than the configured amount when you submit
    a job, you should be able to use the extra cores.
    """

    job_id = atf.submit_job(f"-w {node_names} --core-spec=0 -n{total_cores} --wrap='srun true'")
    atf.wait_for_job_state(job_id, "DONE")

    output = int(re.findall(
        rf'{job_id}\.0\s+(\d+)',
        atf.run_command_output(f"sacct -j {job_id} -o jobid%20,alloccpus"))[0])

    assert output == total_cores, f"--core-spec=0 should allow {total_cores} cores"

    job_id = atf.submit_job(f"-w {node_names} --core-spec=0 --wrap='srun true'")
    atf.wait_for_job_state(job_id, "DONE")

    output = int(re.findall(
        rf'{job_id}\.0\s+(\d+)',
        atf.run_command_output(f"sacct -j {job_id} -o jobid%20,alloccpus"))[0])

    assert output == total_cores, f"Using --core-spec should imply --exclusive and using all cores"

    job_id = atf.submit_job(f"-w {node_names} --core-spec=1 -n{total_cores - 2} --wrap='srun true'")
    atf.wait_for_job_state(job_id, "DONE")

    output = int(re.findall(
        rf'{job_id}\.0\s+(\d+)',
        atf.run_command_output(f"sacct -j {job_id} -o jobid%20,alloccpus"))[0])

    assert output == total_cores - 2, f"--core-spec=1 should allocate all cores except 1 per node"

    job_id = atf.submit_job(f"-w {node_names} --core-spec=2 -n{total_cores - 4} --wrap='srun true'")
    atf.wait_for_job_state(job_id, "DONE")

    output = int(re.findall(
	    rf'{job_id}\.0\s+(\d+)',
	    atf.run_command_output(f"sacct -j {job_id} -o jobid%20,alloccpus"))[0])

    assert output == total_cores - 4, f"--core-spec=2 should allocate all cores except 2 per node"

    exit_code = atf.run_job_exit(f" -w {node_names} -N2 --core-spec=2 -n{available_cores} true", xfail=True)
    assert exit_code != 0, "--core-spec limits the available cores in nodes"


def test_thread_spec_override(node_names):
    """Verify that if you use the --thread-spec option with less than the configured amount when you submit
    a job, you should be able to use the extra threads.
    """

    job_id = atf.submit_job(f"-w {node_names} --thread-spec=1 --wrap='srun true'")
    atf.wait_for_job_state(job_id, "DONE")

    output = int(re.findall(
        rf'{job_id}\.0\s+(\d+)',
        atf.run_command_output(f"sacct -j {job_id} -o jobid%20,alloccpus"))[0])

    assert output == (total_cpus - 2), f"--thread-spec=1 should override and reserve 1 cpu per node (2 total) for the batch step"
