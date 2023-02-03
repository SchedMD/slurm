############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_config_parameter('PriorityType', 'priority/multifactor')
    atf.require_slurm_running()


def test_weights():
    """Verify sprio --weights has the correct output"""

    output = atf.run_command_output(f"sprio --weights", fatal=True)
    assert re.search(r'JOBID\s+PARTITION\s+PRIORITY\s+SITE', output) is not None
    assert re.search(r'Weights.*\d+', output) is not None
