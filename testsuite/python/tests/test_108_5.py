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
    """Verify scontrol --json has the correct format"""

    output = atf.run_command_output(f"scontrol --json {action}", fatal=True)
    assert json.loads(output) is not None
