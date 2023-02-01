############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_version():
    """Verify sprio --version has the correct format"""

    output = atf.run_command_output(f"sprio --version", fatal=True)
    assert re.search(r'slurm \d+\.\d+\.\d+', output) is not None
