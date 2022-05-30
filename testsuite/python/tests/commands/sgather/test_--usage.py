############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sgather --usage has the correct format"""

    output = atf.run_command_output(f"sgather --usage", fatal=True)
    assert re.search(r'Usage: sgather \[-[A-Za-z]+\] SOURCE DEST$', output) is not None
