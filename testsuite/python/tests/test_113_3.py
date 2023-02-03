############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

def test_help():
    """Verify sprio --help displays the help page"""

    output = atf.run_command_output(f"sprio --help", fatal=True)

    assert re.search(r'sprio \[OPTIONS\]', output) is not None
    assert re.search(r'-j, --jobs', output) is not None
    assert re.search(r'-l, --long', output) is not None
    assert re.search(r'-M, --cluster', output) is not None
    assert re.search(r'-p, --partition', output) is not None
    assert re.search(r'-u, --user', output) is not None
    assert re.search(r'-w, --weights', output) is not None
