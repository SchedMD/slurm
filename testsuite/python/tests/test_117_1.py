############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_version():
    """Verify sstat --version looks reasonable"""

    output = atf.run_command_output(f"sstat --version", fatal=True)
    assert re.search(r'slurm \d+\.\d+\.\d+', output) is not None
