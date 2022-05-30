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
    atf.require_config_parameter_includes('GresTypes', 'gpu')
    atf.require_config_parameter_includes('GresTypes', 'mps')
    atf.require_tty(0)
    atf.require_config_parameter('Name', {'gpu': {'File': '/dev/tty0'}, 'mps': {'Count': 100}}, source='gres')
    atf.require_nodes(1, [('Gres', 'gpu:1,mps:100')])
    atf.require_slurm_running()


def test_mps_and_gpus():
    """Test with both GPUs and MPS in a single request"""
    results = atf.run_command("sbatch --gres=mps:1,gpu:1 -N1 -t1 --wrap \"true\"")
    assert results['exit_code'] != 0
    assert re.search(r"Invalid generic resource \(gres\) specification", results['stderr']) is not None


def test_mps_and_gpu_frequency():
    """Test with both GPUs and MPS in a single request"""
    results = atf.run_command("sbatch --gres=mps:1 --gpu-freq=high -N1 -t1 --wrap \"true\"")
    assert results['exit_code'] != 0
    assert re.search(r"Invalid generic resource \(gres\) specification", results['stderr']) is not None


# Request MPS per job with node count > 1
# Request MPS per socket with socket count > 1
# Request MPS per task with task count > 1
#
# TODO: Add these tests whenever tres-per-* options added
# The tests already exist in src/common/gres.c to reject such jobs
