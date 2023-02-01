############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_kill_on_bad_exit():
    """Test of srun's --kill-on-bad-exit option."""

    error_text = "SHOULD_NOT_BE_HERE"
    python_script = atf.module_tmp_path / 'python_script.py'
    py_file = open(python_script, 'w')
    num_tasks = 10

    # python script that exits on one of the tasks of the job
    # if any task gets past the sleep then the test fails
    py_file.write(f"""import sys
import time
import os
proc_id = os.environ['SLURM_PROCID']
if proc_id == '{num_tasks - 2}':
    sys.exit(2)
time.sleep(15)
print('{error_text}')""")
    py_file.close()

    output = atf.run_job_output(f"--kill-on-bad-exit -n{num_tasks} -O python3 {python_script}", xfail=True)
    assert re.search(error_text, output) is None, f"--kill-on-bad-exit failed to kill all tasks on a bad exit"
