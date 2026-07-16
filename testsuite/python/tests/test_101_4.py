############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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


def test_json():
    """Verify sacct --json has the correct format and meta data command"""

    expected_command = ["sacct", "--json"]

    output = atf.run_command_output("sacct --json", fatal=True)
    json_data = json.loads(output)
    assert json_data is not None
    if atf.get_version("bin/sacct") >= (26, 5):
        assert json_data["meta"]["command"] == expected_command
