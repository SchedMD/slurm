############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import pexpect


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_unbuffered(tmp_path):
    """Verify srun --unbuffered"""

    # Submit a slurm job that will execute 'rm -i'
    file_in = str(tmp_path / "file_in.input")
    atf.make_bash_script(file_in, """""")
    child = pexpect.spawn(f'srun -t2 --unbuffered --verbose rm -f -i {file_in}')
    assert child.expect(r'remove.*?file') is not None, 'rm prompt not found'
    child.sendline('y')
