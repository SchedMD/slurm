############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sattach --usage has the correct format"""

    output = atf.run_command_output(f"sattach --usage", fatal=True)
    assert re.search(r'Usage: sattach \[.*\] <jobid\.stepid>$', output) is not None
