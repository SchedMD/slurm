############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_help():
    """Verify sinfo --help displays the help page"""

    output = atf.run_command_output(f"sinfo --help", fatal=True)

    assert re.search(r'Usage: sinfo \[OPTIONS\]', output) is not None
    assert re.search(r'Help options:', output) is not None
