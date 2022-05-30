############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import os

node_num = 3


def write_host_file(matchs):
    host_file = atf.module_tmp_path / 'host_file'
    hf = open(host_file, 'w')
    for line in matchs:
        hf.write(line[1] + '\n')
    hf.seek(0)
    hf.close


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():    
    atf.require_nodes(node_num + 2)
    atf.require_config_parameter('FrontendName', None)
    atf.require_slurm_running()


def test_hostfile():
    """Test of hostfile option (-hostfile)."""

    host_file = atf.module_tmp_path / 'host_file'
    HOSTFILE_ENV = 'SLURM_HOSTFILE'

    output = atf.run_job_output(f"-N{node_num} -l printenv SLURMD_NODENAME")
    match1 = re.findall(r'(\d+): (\S+)', output)
    match1 += [match1.pop(0)]
    match_ordered = []
    for iter in range(len(match1)):
        match_ordered.append((str(iter), match1[iter][1]))
    os.environ[HOSTFILE_ENV] = str(host_file)
    write_host_file(match1)

    # Test pass 1
    output = atf.run_job_output(f"-N{node_num} -l --distribution=arbitrary printenv SLURMD_NODENAME")
    match2 = re.findall(r'(\d+): (\S+)', output)
    for match in match2:
        assert match in match_ordered, f"On pass 1 Task {match[0]} not distributed by hostfile {match} not in {match_ordered}"

    match2 += [match2.pop(0)]
    match_ordered = []
    for iter in range(len(match1)):
        match_ordered.append((str(iter), match2[iter][1]))
    write_host_file(match2)
    
    # Test pass 2
    output = atf.run_job_output(f"-N{node_num} -l --distribution=arbitrary printenv SLURMD_NODENAME")
    match3 = re.findall(r'(\d+): (\S+)', output)
    for match in match3:
        assert match in match_ordered, f"On pass 2 Task {match[0]} not distributed by hostfile {match} not in {match_ordered}"
