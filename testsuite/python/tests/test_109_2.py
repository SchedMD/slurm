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
    atf.require_slurm_running()


def test_json():
    """Verify sdiag --json has the correct format and meta data command"""

    expected_command = ["sdiag", "--json"]

    output = atf.run_command_output("sdiag --json", fatal=True)
    json_data = json.loads(output)
    assert json_data is not None
    if atf.get_version("bin/sdiag") >= (26, 5):
        assert json_data["meta"]["command"] == expected_command
