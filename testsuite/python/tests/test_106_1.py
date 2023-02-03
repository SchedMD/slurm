############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sbcast --usage has the correct format"""

    output = atf.run_command_output(f"sbcast --usage", fatal=True)
    assert re.search(r'Usage: sbcast (?:\[-+[a-zA-Z-]+]\s)+SOURCE DEST\n$', output) is not None
