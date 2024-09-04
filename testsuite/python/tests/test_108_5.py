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
    """Verify scontrol --json has the correct format"""

    output = atf.run_command_output("scontrol show licenses --json", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output("scontrol ping --json", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output("scontrol show jobs --json", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output("scontrol show job --json", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output("scontrol show steps --json", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output("scontrol show nodes --json", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output("scontrol show partitions --json", fatal=True)
    assert json.loads(output) is not None

    output = atf.run_command_output("scontrol show reservations --json", fatal=True)
    assert json.loads(output) is not None
