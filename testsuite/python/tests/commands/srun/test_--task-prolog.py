############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import os
import pytest
import shutil

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()

def test_task_prolog(tmp_path):
    """Test srun --task-prolog"""

    sleep_symlink = str(tmp_path / f"sleep_symlink_{os.getpid()}")
    os.symlink(shutil.which('sleep'), sleep_symlink)
    task_prolog = str(tmp_path / 'task_prolog.sh')
    touched_file = str(tmp_path / 'touched_file')
    atf.make_bash_script(task_prolog, f"""touch {touched_file}
exit 0
""")

    # Verify task prolog ran by checking for the file creation
    atf.run_job(f"-t1 --task-prolog={task_prolog} true", fatal=True)
    assert os.path.isfile(touched_file), f"File ({touched_file}) was not created"
