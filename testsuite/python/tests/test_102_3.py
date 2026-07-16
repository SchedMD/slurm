############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
# import re
import json

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "entry", ["accounts", "associations", "clusters", "qos", "wckeys", "users", "tres"]
)
def test_json(entry):
    """Verify sacctmgr --json has the correct format and meta data command"""

    expected_command = ["sacctmgr", "--json", "show", entry]

    output = atf.run_command_output(f"sacctmgr --json show {entry}", fatal=True)
    json_data = json.loads(output)
    assert json_data is not None
    if atf.get_version("bin/sacctmgr") >= (26, 5):
        assert json_data["meta"]["command"] == expected_command
