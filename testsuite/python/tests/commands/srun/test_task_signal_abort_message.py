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


def test_task_signal_abort_message():
    """Test of task signal abort message"""

    error_msg = 'Segmentation fault'
    file_in = atf.module_tmp_path / 'file_in'
    atf.make_bash_script(file_in, "kill -11 $$")
    run_error = atf.run_command_error(f"srun {file_in}", xfail=True)
    assert re.search(error_msg, run_error) is not None, "Srun failed to send message after segfault"
