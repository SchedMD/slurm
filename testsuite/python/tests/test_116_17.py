############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_v():
    output = atf.run_command_error("srun -v id")
    assert re.search(r"verbose\s+: 1", output) is not None

    output = atf.run_command_error("srun -vvvv id")
    assert re.search(r"verbose\s+: 4", output) is not None
