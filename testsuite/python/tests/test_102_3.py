############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

# import re
import json


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "entry", ["accounts", "associations", "clusters", "qos", "wckeys", "users", "tres"]
)
def test_json(entry):
    """Verify sacctmgr --json has the correct format"""

    output = atf.run_command_output(f"sacctmgr --json show {entry}", fatal=True)
    assert json.loads(output) is not None
