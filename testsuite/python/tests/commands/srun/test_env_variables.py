############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import pexpect
import os
import re

suser=atf.properties['slurm-user']


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_env_variables():
    """Verify the appropriate job environment variables are set."""

    env_vars = []
    env_vars_g0 = [] #variables that need to be greater than zero

    if atf.get_config_parameter("FrontendName") is not None:
        env_vars.append('SLURM_LAUNCH_NODE_IPADDR')
        env_vars.append('SLURM_LOCALID')
        env_vars.append('SLURM_NNODES')
        env_vars.append('SLURM_NODEID')
        env_vars.append('SLURM_NODELIST')
        env_vars.append('SLURM_PROCID')
        env_vars.append('SLURM_SRUN_COMM_HOST')
        env_vars.append('SLURM_STEPID')
        env_vars_g0.append('SLURM_CPUS_ON_NODE')
        env_vars_g0.append('SLURM_CPUS_PER_TASK')
        env_vars_g0.append('SLURM_NTASKS')
        env_vars_g0.append('SLURM_SRUN_COMM_PORT')
        env_vars_g0.append('SLURM_TASKS_PER_NODE')
        env_vars_g0.append('SLURM_TASK_PID')
        env_vars_g0.append('SLURM_JOB_ID')
    else:
        env_vars.append('SLURM_LAUNCH_NODE_IPADDR')
        env_vars.append('SLURM_LOCALID')
        env_vars.append('SLURM_NNODES')
        env_vars.append('SLURM_NODEID')
        env_vars.append('SLURM_NODELIST')
        env_vars.append('SLURM_PROCID')
        env_vars.append('SLURM_SRUN_COMM_HOST')
        env_vars.append('SLURM_STEPID')
        env_vars.append('SLURM_TOPOLOGY_ADDR')
        env_vars.append('SLURM_TOPOLOGY_ADDR_PATTERN')
        env_vars_g0.append('SLURM_CPUS_ON_NODE')
        env_vars_g0.append('SLURM_CPUS_PER_TASK')
        env_vars_g0.append('SLURM_JOB_ID')
        env_vars_g0.append('SLURM_NTASKS')
        env_vars_g0.append('SLURM_SRUN_COMM_PORT')
        env_vars_g0.append('SLURM_TASKS_PER_NODE')
        env_vars_g0.append('SLURM_TASK_PID')

    env_vars_list = atf.run_job_output("-N1 -n1 --cpus-per-task=1 env", user=suser).splitlines()
    env_var_count = 0
    env_var_g0_count = 0
    for env_var in env_vars_list:
        name_val = env_var.split('=')
        name = name_val[0]
        env_value = name_val[1]
        if name in env_vars:
            env_var_count += 1
        elif name in env_vars_g0:
            if int(env_value) > 0:
                env_var_g0_count += 1
    assert env_var_count == len(env_vars), f"Not all environment variables are set missing {len(env_vars) - env_var_count}"
    assert env_var_g0_count == len(env_vars_g0), f"Not all environment variables are set and greater than zero missing {len(env_vars_g0) - env_var_g0_count}"


def test_user_env_variables():
    """Verify that user environment variables are propagated to the job."""

    file_in = atf.module_tmp_path / 'env_script'
    env_var = 'TEST_ENV_VAR'
    env_val = '123'
    atf.make_bash_script(file_in,f"""env | grep {env_var}; exit 0""")
    output = atf.run_job_output(f'--export={env_var}={env_val} {file_in}').strip()
    assert output == f'{env_var}={env_val}', "Environment variables not propagated with --export"

    child = pexpect.spawn(f'env {env_var}={env_val}')
    child.sendline(f'srun {file_in}')
    assert child.expect([f'{env_var}={env_val}', pexpect.EOF]) == 0, "Environment variables not propagated"
    child.close()

    os.environ[env_var] = env_val
    output = atf.run_job_output(f'--export=ALL {file_in}').strip()
    assert output == f'{env_var}={env_val}', "Environment variables not propagated with export=ALL"

    output = atf.run_job_output(f'--export=NONE {file_in}').strip()
    assert output != f'{env_var}={env_val}', "Environment variables were propagated with export=NONE"


def test_slurm_directed_env_variables():
    """Verify that Slurm directed environment variables are processed: SLURM_DEBUG, SLURM_NNODES, SLURN_NPROCS, SLURM_OVERCOMMIT, SLURM_STDOUTMODE."""

    file_in = atf.module_tmp_path / 'file_in'
    file_out = str(atf.module_tmp_path / 'file_out.output')
    sorted_file_out = atf.module_tmp_path / 'sorted_file_out'

    min_nodes = 1
    max_nodes = 2 
    slurm_debug = 'SLURM_DEBUG'
    slurm_debug_val = '1'
    slurm_nnodes = 'SLURM_NNODES'
    slurm_nnodes_val = str(max_nodes-min_nodes)
    slurm_nprocs = 'SLURM_NPROCS'
    slurm_nprocs_val = '5'
    slurm_stdoutmode = 'SLURM_STDOUTMODE'
    slurm_stdoutmode_val = file_out
    slurm_overcommit = 'SLURM_OVERCOMMIT'
    slurm_overcommit_val = '1'

    os.environ[slurm_debug] = slurm_debug_val
    os.environ[slurm_nnodes] = slurm_nnodes_val
    os.environ[slurm_nprocs] = slurm_nprocs_val
    os.environ[slurm_stdoutmode] = slurm_stdoutmode_val
    os.environ[slurm_overcommit] = slurm_overcommit_val

    atf.make_bash_script(file_in, "env | grep SLURM_")
    atf.run_job(f'{file_in}')
    atf.wait_for_file(f'{file_out}')
    atf.run_command(f"sort {file_out} > {sorted_file_out}")

    f = open(sorted_file_out, 'r')
    output = f.read()
    f.close()

    task_count = len(re.findall(rf'{slurm_nprocs}=(\d+)', output))
    stale_count = len(re.findall(r'Stale file handle', output))
    task_count += stale_count
    assert task_count == int(slurm_nprocs_val), f"Did not process {slurm_nprocs} environment variable ({task_count} != {slurm_nprocs_val})"

    match = re.findall(r'SLURM_NODEID=(\d+)', output)

    #add one since nodeID starts at zero
    max_node_val = int(max(match)) + 1
    assert max_node_val >= min_nodes and max_node_val <= max_nodes, f"Did not process {slurm_nnodes} environment variable max_node_val = {max_node_val}"

    assert re.search(f'{slurm_debug}=1',output) is not None, f"Did not process {slurm_debug} environment variable"
    assert re.search(f'{slurm_overcommit}=1',output) is not None, f"Did not process {slurm_overcommit} environment variable"
