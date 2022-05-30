############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sdiag --usage has the correct format"""

    output = atf.run_command_output(f"sdiag --usage", fatal=True)
    assert re.search(r'Usage: sdiag(?:\s+\[-{1,2}[^\]]+\])+$', output) is not None
