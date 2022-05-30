############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_help():
    """Verify squeue --help displays the help page"""

    output = atf.run_command_output(f"squeue --help", fatal=True)

    assert re.search(r'Usage: squeue \[OPTIONS\]', output) is not None
    assert re.search(r'Help options:', output) is not None
