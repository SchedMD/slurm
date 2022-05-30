############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sprio --usage has the correct format"""

    output = atf.run_command_output(f"sprio --usage", fatal=True)
    assert re.search(r'Usage: sprio(?:\s+\[-{1,2}[^\]]+(?:\[s\])?\])+$', output) is not None
