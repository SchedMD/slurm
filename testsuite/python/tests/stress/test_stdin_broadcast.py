############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import os
import pytest
import re

iteration_count = 100
node_count = 4
task_count = 4

# Setup/Teardown
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_slurm_running()
    atf.require_nodes(node_count, [('CPUs', task_count)])

@pytest.fixture(scope='function')
def input_file(request, tmp_path):
    """Create a sizable text file"""
    in_file = str(tmp_path / "test.in")
    with open(in_file, 'w') as f:
        for line in range(0,100):
            for i in range(1,100):
                f.write(chr(ord('0') + i % 10))
            f.write('\n')
    return in_file

def test_stdin_broadcast(input_file, tmp_path):
    """Copy job input to job output and compare sizes"""
    output_file = str(tmp_path / "test.out")
    for i in range(0, iteration_count):
        if os.path.isfile(output_file):
            os.remove(output_file)
        exit_code = atf.run_command_exit(f"srun --input={input_file} --output={output_file} --error=/dev/null -n {task_count} -N {node_count} -t 1 cat -")
        assert exit_code == 0, f"srun failed with status {exit_code}"
        in_size = os.path.getsize(input_file)
        out_size = os.path.getsize(output_file)
        assert out_size == in_size * task_count, f"output file size ({out_size}) was not {task_count} times the input file size ({in_size})"
