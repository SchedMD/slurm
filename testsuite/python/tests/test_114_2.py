############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify squeue --usage has the correct format"""

    output = atf.run_command_output(f"squeue --usage", fatal=True)
    assert re.search(r'Usage: squeue(?:\s+\[-{1,2}[^\]]+\])+$', output) is not None
