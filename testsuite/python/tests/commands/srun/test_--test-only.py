############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

node_count = 9


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_slurm_running()


def test_test_only():
    """Test of slurm_job_will_run API, (srun --test-only option)."""

    error_output = atf.run_command_error(f"srun --test-only -O -N{node_count} printenv SLURMD_NODENAME")
    assert re.search(r'Job \d+ to start at \d+', error_output) is not None, "Failed out output job number or start time"
