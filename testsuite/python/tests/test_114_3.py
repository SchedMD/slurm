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
    atf.require_slurm_running()


def test_json():
    """Verify squeue --json has the correct format and meta data command"""

    expected_command = ["squeue", "--json"]

    output = atf.run_command_output("squeue --json", fatal=True)
    json_data = json.loads(output)
    assert json_data is not None
    if atf.get_version("bin/squeue") >= (26, 5):
        assert json_data["meta"]["command"] == expected_command
