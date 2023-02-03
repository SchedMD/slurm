############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import json

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()


def test_json():
    """Verify sacctmgr --json has the correct format"""

    output = atf.run_command_output(f"sacctmgr --json show accounts", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output(f"sacctmgr --json show associations", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output(f"sacctmgr --json show clusters", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output(f"sacctmgr --json show qos", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output(f"sacctmgr --json show wckeys", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output(f"sacctmgr --json show users", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output(f"sacctmgr --json show tres", fatal=True)
    assert json.loads(output) is not None
