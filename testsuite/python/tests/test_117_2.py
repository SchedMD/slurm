############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

def test_help():
    """Verify sstat --help displays the help page"""

    output = atf.run_command_output(f"sstat --help", fatal=True)

    assert re.search(r'sstat \[<OPTION>\]', output) is not None
    assert re.search(r'Valid <OPTION> values are:', output) is not None
    assert re.search(r'-j, --jobs:', output) is not None
