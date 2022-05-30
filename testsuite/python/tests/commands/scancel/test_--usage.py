############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify scancel --usage has the correct format"""

    output = atf.run_command_output(f"scancel --usage", fatal=True)
    assert re.search(r'Usage: scancel(?:\s+\[-{1,2}[^\]]+\])+\s+\[job_id.+\]$', output) is not None
