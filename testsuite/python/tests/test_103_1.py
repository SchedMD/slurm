############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify salloc --usage has the correct format"""

    output = atf.run_command_output(f"salloc --usage", fatal=True)
    assert re.search(r'Usage: salloc(?:\s+\[{1,2}-{1,2}.*?\](?=\s+\[))+\s+\[command \[args\.\.\.\]\]$', output) is not None
