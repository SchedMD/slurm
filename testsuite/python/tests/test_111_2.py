############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sinfo --usage has the correct format"""

    output = atf.run_command_output(f"sinfo --usage", fatal=True)
    assert re.search(r'Usage: sinfo(?:\s+\[-{1,2}[^\]]+])+$', output) is not None
