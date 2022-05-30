############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import pexpect
import re
import time


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_quit_on_interrupt():
    """Test srun handling of SIGINT to get task status or kill the job (--quit-on-interrupt option)."""

    file_in = atf.module_tmp_path / 'file_in'
    atf.make_bash_script(file_in, """trap \"\" INT
echo WAITING
sleep 1000""")

    child = pexpect.spawn(f"srun -v -N1 --unbuffered {file_in}")
    child.expect(r"launching StepId=(\d+)")
    step_id = re.search(r'\d+',str(child.after)).group(0)
    child.expect('WAITING')
    time.sleep(1)
    atf.run_command(f"kill -INT {child.pid}")
    assert child.expect("srun: interrupt") == 0, f"srun failed to process command (kill -INT {child.pid})"
    atf.run_command(f"scancel {step_id}")
    assert child.expect("Force Terminated job") == 0, f"srun failed to process command (scancel {step_id})"
    child.close()

    child = pexpect.spawn(f"srun -v -N1 --unbuffered --quit-on-interrupt {file_in}")
    child.expect(r"launching StepId=(\d+)")
    step_id = re.search(r'\d+',str(child.after)).group(0)
    child.expect('WAITING')
    time.sleep(1)
    atf.run_command(f"kill -INT {child.pid}")
    assert child.expect("task 0: Killed") == 0, "Did not get message (task 0: Killed) from srun"
