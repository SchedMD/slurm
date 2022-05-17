############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import os
import pytest
import re

node_count = 1

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():    
    atf.require_slurm_running()
    atf.require_nodes(node_count)

def test_wait(tmp_path):
    """Verify srun --wait"""
    task_count = 10
    file_in = str(tmp_path / "file_in.input")
    atf.make_bash_script(file_in, """if [[ -z "$SLURM_PROCID" ]]
then exit
fi
if [[ $SLURM_PROCID == 1 ]]
then exit
fi
sleep 20
""")
    os.chmod(file_in, 0o0755)
    run_error = atf.run_command_error(f'srun -n{task_count} -N{node_count} -O -W2 {file_in}')
    task_count -= 1
    assert re.search('First task exited', run_error) is not None, 'First task did not exit'
    assert re.search('tasks 0,2-9: running', run_error) is not None, 'Other tasks not running'
    assert re.search('tasks 0,2-9: Killed', run_error) is not None, 'Other tasks not killed'
