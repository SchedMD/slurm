############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os

total_cores = 0
available_cores = 0
allowed_cpu_list = []


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to set CPUSpecList on a node")
    atf.require_config_parameter('SelectType', 'select/cons_res')
    atf.require_config_parameter('TaskPlugin', 'task/affinity')
    atf.require_config_parameter('AllowSpecResourcesUsage', '1')
    atf.require_nodes(1, [('Cores', 2)])
    atf.require_config_parameter('ConstrainCores', 'yes', source='cgroup')
    atf.require_slurm_running()


# Assumes default value of ThreadsPerCore=1
@pytest.fixture(scope="module")
def node_name():
    global total_cores, available_cores, allowed_cpu_list

    node = atf.run_job_nodes(f"--cpu-bind=core -N1 -n2 true")[0]
    sockets = atf.get_node_parameter(node, 'Sockets')
    cores_per_socket = atf.get_node_parameter(node, 'CoresPerSocket')

    total_cores = sockets * cores_per_socket
    available_cores = total_cores - 1 
    allowed_cpu_list = create_cpu_list(node)

    # Reserve the lowest cpu id# (first) on our CPUSpecList
    atf.set_node_parameter(node, 'CPUSpecList', allowed_cpu_list[0])
    return node


# Helper function, formats different cpu id outputs into an expanded + sorted comma separated list
def create_cpu_list(node):
    output_list = atf.run_command_output(f"srun --exclusive -w {node} grep Cpus_allowed_list /proc/self/status | awk '{{print $2}}'").strip().split(",")

    # Possible formats after parsing:
    # output_list = ['0']
    # output_list = ['0-49']
    # output_list = ['0', '2-4', '10', etc..]

    result = atf.range_to_list(','.join(output_list))
    return sorted(result)


# Tests:
def test_job_submit(node_name):
    """Verify a job requesting a proper number of cpus is submitted with CPUSpecList plugin enabled"""

    job_id = atf.run_job_id(f"-w {node_name} -N1 -n{available_cores} true")
    output = atf.run_command_output(f"scontrol show job {job_id} -dd | grep CPU_IDs= | awk '{{print $2}}' | sed 's/^.*CPU_IDs=//'")
    cpu_spec_list = atf.get_node_parameter(node_name, 'CPUSpecList')

    assert atf.get_job_parameter(job_id, 'ExitCode')  == '0:0', "Job should submit with -n = the number of available cores and CPUSpecList enabled"
    assert output != cpu_spec_list, f"Cpus reserved in CPUSpecList ({cpu_spec_list}) should not be used for the job (job {'job_id'} CPU_IDs={output})"


def test_job_denied(node_name):
    """Verify a job requesting too many cpus is rejected with CPUSpecList plugin enabled"""

    # Need to add a -N1 to restrict it to the node as it was decided that slurm would override
    # the conf when the ThreadsPerCore=1 doesn't match the hardware and thus allows multiple
    # tasks on a core to go through.
    # See Bug 10613, ~ comment 24: https://bugs.schedmd.com/show_bug.cgi?id=10613#c24

    exit_code = atf.run_job_exit(f"-w {node_name} -N1 -n{total_cores} true")
    assert exit_code != 0, "Job should be rejected when -n = the number of total cores and CPUSpecList is enabled"


def test_AllowSpecResourcesUsage(node_name):
    """Verify AllowSpecResourcesUsage override functionality works"""

    exit_code = atf.run_job_exit(f" -w {node_name} -N1 --core-spec=0 -n{total_cores} true")
    assert exit_code == 0, "AllowSpecResourceUsage should allow job to run"


def test_cpu_ids(node_name):
    """Verify job does not run on our CPU reserved in CPUSpecList"""

    # Assert job is rejected on our allowed cpu reserved on CPUSpecList
    exit_code  = atf.run_job_exit(f"-w {node_name} -n1 --cpu-bind=verbose,map_cpu:{allowed_cpu_list[0]} hostname")
    assert exit_code != 0, f"Job should not run on cpu id: {allowed_cpu_list[0]} reserved in CPUSpecList"

    # Assert job is allowed on our other allowed cpu(s) not reserved on CPUSpecList (if any)
    if len(allowed_cpu_list) > 1:
        cpus_to_str = atf.list_to_range(allowed_cpu_list[1:])
        exit_code = atf.run_job_exit(f"-w {node_name} -n{len(allowed_cpu_list) - 1} --cpu-bind=verbose,map_cpu:{cpus_to_str} hostname")
        assert exit_code == 0, f"Job should run on our other allowed cpu id(s): {cpus_to_str} not reserved in CPUSpecList"
