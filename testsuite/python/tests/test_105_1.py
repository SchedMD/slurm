############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('SelectType', 'select/cons_tres')
    atf.require_tty(0)
    atf.require_config_parameter('Name', {'gpu': {'File': '/dev/tty0'}}, source='gres')
    atf.require_config_parameter_includes('GresTypes', 'gpu')
    atf.require_nodes(1, [('Gres', 'gpu:1'), ('RealMemory', 1)])
    atf.require_slurm_running()


# Global variables set via init_gpu_vars
node_count = 1
gpus_per_node = 1
gpu_count = 1
cpus_per_gpu = 1
sockets_per_node = 1
task_count = 1
gpus_per_task = 1
memory_per_gpu = 1


@pytest.fixture(scope="module")
def init_gpu_vars():
    global node_count, gpus_per_node, gpu_count, task_count, gpus_per_task, mem_per_gpu

    nodes_with_gpus = 0
    min_gpus_per_node = 1024
    min_cpus_per_node = 1024
    min_sockets_per_node = 1024
    min_memory_per_node = 1024

    for node_name in atf.nodes:
        node_dict = atf.nodes[node_name]
        if 'Gres' in node_dict and node_dict['Gres'] is not None:
            if match := re.search(r'gpu:(\d+)', node_dict['Gres']):
                nodes_with_gpus += 1
                node_gpu_count = int(match.group(1))
                node_cpu_count = node_dict['CPUTot']
                node_memory = node_dict['RealMemory']
                node_socket_count = node_dict['Sockets']
                if node_cpu_count < node_gpu_count:
                    node_gpu_count = node_cpu_count
                if node_gpu_count < min_gpus_per_node:
                    min_gpus_per_node = node_gpu_count
                if node_cpu_count < min_cpus_per_node:
                    min_cpus_per_node = node_cpu_count
                if node_socket_count < min_sockets_per_node:
                    min_sockets_per_node = node_socket_count
                if node_memory < min_memory_per_node:
                    min_memory_per_node = node_memory

    node_count = nodes_with_gpus
    gpus_per_node = min_gpus_per_node
    gpu_count = gpus_per_node * node_count
    sockets_per_node = min_sockets_per_node
    if gpus_per_node % 2 == 0 and min_cpus_per_node > 1:
        task_count = node_count * 2
    else:
        task_count = node_count
    gpus_per_task = int(gpu_count / task_count)
    memory_per_gpu = int(min_memory_per_node/min_gpus_per_node)
    if memory_per_gpu < 1:
        pytest.skip("This test requires at least one node with {min_gpus_per_node} memory")


def test_gpus_per_cpu(init_gpu_vars):
    """Test a batch job with various gpu options including ---gpus"""

    gpu_bind = 'closest'
    gpu_freq = 'medium'

    job_id = atf.submit_job(f"--cpus-per-gpu={cpus_per_gpu} --gpu-bind={gpu_bind} --gpu-freq={gpu_freq} --gpus={gpu_count} --gpus-per-node={gpus_per_node} --gpus-per-task={gpus_per_task} --mem-per-gpu={memory_per_gpu} --nodes={node_count} --ntasks={task_count} -t1 --wrap \"true\"", fatal=True)
    job_dict = atf.get_job(job_id)

    assert job_dict['CpusPerTres'] == f"gres:gpu:{cpus_per_gpu}"
    assert job_dict['MemPerTres'] == f"gres:gpu:{memory_per_gpu}"
    assert job_dict['TresBind'] == f"gpu:{gpu_bind}"
    assert job_dict['TresFreq'] == f"gpu:{gpu_freq}"
    assert job_dict['TresPerJob'] == f"gres:gpu:{gpu_count}"
    assert job_dict['TresPerNode'] == f"gres:gpu:{gpus_per_node}"
    assert job_dict['TresPerTask'] == f"gres:gpu:{gpus_per_task}"


def test_gpus_per_socket(init_gpu_vars):
    """Test a batch job with various gpu options including --gpus-per-socket"""
    gpus_per_socket = 1

    job_id = atf.submit_job(f"--cpus-per-gpu={cpus_per_gpu} --gpus-per-socket={gpus_per_socket} --sockets-per-node={sockets_per_node} --nodes={node_count} --ntasks={task_count} -t1 --wrap \"true\"", fatal=True)
    job_dict = atf.get_job(job_id)

    assert job_dict['TresPerSocket'] == f"gres:gpu:{gpus_per_socket}"
