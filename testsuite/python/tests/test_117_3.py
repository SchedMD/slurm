############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sstat --usage has the correct format"""

    output = atf.run_command_output(f"sstat --usage", fatal=True)
    assert re.search(r'Usage: sstat \[options\] -j <job.*step', output) is not None
    assert re.search(r'Use --help for help', output) is not None
