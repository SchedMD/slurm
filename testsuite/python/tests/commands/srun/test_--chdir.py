############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_chdir():
    """Verify srun --chdir sets appropriate working directory"""

    tmp_dir = "/tmp"
    output = atf.run_command_output(f"srun --chdir=" + tmp_dir + " pwd", fatal=True)
    assert output.strip("\n") == tmp_dir
