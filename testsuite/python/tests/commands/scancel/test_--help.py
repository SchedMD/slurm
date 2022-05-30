############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_help():
    """Verify scancel --help displays the help page"""

    output = atf.run_command_output(f"scancel --help", fatal=True)

    assert re.search(r'Usage: scancel \[OPTIONS\]', output) is not None
    assert re.search(r'Help options:', output) is not None
