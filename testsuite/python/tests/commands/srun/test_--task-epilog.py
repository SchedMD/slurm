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

def test_task_epilog(tmp_path):
    """Test srun --task-epilog"""

    sleep_symlink = str(tmp_path / f"sleep_symlink_{os.getpid()}")
    os.symlink(shutil.which('sleep'), sleep_symlink)
    task_epilog = str(tmp_path / 'task_epilog.sh')
    touched_file = str(tmp_path / 'touched_file')
    atf.make_bash_script(task_epilog, f"""touch {touched_file}
{sleep_symlink} 60 &
exit 0
""")

    # Verify task epilog ran by checking for the file creation
    atf.run_job(f"-t1 --task-epilog={task_epilog} true", fatal=True)
    assert os.path.isfile(touched_file), f"File ({touched_file}) was not created"

    # Verify that the child processes of the epilog have been killed off
    assert atf.run_command_exit(f"pgrep {sleep_symlink}", xfail=True) != 0, "Process ({sleep_symlink}) should have been killed"
