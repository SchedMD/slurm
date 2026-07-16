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


@pytest.mark.parametrize(
    "action",
    [
        "show licenses",
        "ping",
        "show jobs",
        "show job",
        "show steps",
        "show nodes",
        "show partitions",
        "show reservations",
    ],
)
def test_json(action):
    """Verify scontrol --json has the correct format and meta data command"""

    expected_command = ["scontrol", "--json"]
    expected_command.extend(action.split())

    output = atf.run_command_output(f"scontrol --json {action}", fatal=True)
    json_data = json.loads(output)
    assert json_data is not None
    if atf.get_version("bin/scontrol") >= (26, 5):
        assert json_data["meta"]["command"] == expected_command
