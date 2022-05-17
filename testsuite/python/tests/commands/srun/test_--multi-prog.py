############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(4)
    atf.require_config_parameter('FrontendName', None)
    atf.require_slurm_running()


def test_multi_prog():
    """Test of MPMD (--multi-prog option)."""

    file_in = atf.module_tmp_path / 'file_in'

    # Submit a slurm job that will execute different programs and arguments by task number
    content = r"""1-2   echo task:%t:offset:%o
0,3   echo task:%t:offset:%o"""
    with open(file_in, 'w') as f:
        f.write(content)
    os.chmod(file_in, 0o777)
    output = atf.run_job_output(f"-n4 -O -l --multi-prog {file_in}")
    assert re.search("0: task:0:offset:0", output) is not None, "Label 0 did not have correct task or offset"
    assert re.search("1: task:1:offset:0", output) is not None, "Label 1 did not have correct task or offset"
    assert re.search("2: task:2:offset:1", output) is not None, "Label 2 did not have correct task or offset"
    assert re.search("3: task:3:offset:1", output) is not None, "Label 3 did not have correct task or offset"
    
    # Submit a slurm job that will execute different executables and check debug info
    content = r"""1-2   hostname
0,3   date"""
    with open(file_in, 'w') as f:
        f.write(content)
    error = str(atf.run_job_error(f"--debugger-test -v -n4 -O -l --multi-prog {file_in}", timeout=10, xfail=True))
    match = re.findall(r'executable:\w+', error)
    assert len(match) == 4, "Did not generate full list of executables"

    error_output = atf.run_job_error(f"--debugger-test -v -n5 -O -l --multi-prog {file_in}", xfail=True)
    assert re.search(f"Configuration file {file_in} invalid", error_output), "Did not note lack of a executable for task 5"

    content = r"""1-2   hostname
0,3   echo aaaa \
task:%t:offset:%o bbbb"""
    with open(file_in, 'w') as f:
        f.write(content)
    output = atf.run_job_output(f"-n4 --overcommit -l -t1 --multi-prog {file_in}")
    assert re.search("0: aaaa task:0:offset:0 bbbb", output), "MPMD line continuation first assert"
    assert re.search("3: aaaa task:3:offset:1 bbbb", output), "MPMD line continuation second assert"
