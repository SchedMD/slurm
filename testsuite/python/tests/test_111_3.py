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
    """Verify sinfo --json has the correct format"""

    output = atf.run_command_output("sinfo --json", fatal=True)
    assert json.loads(output) is not None
