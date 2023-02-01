############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify srun --usage looks reasonable"""

    output = atf.run_command_output(f"srun --usage", fatal=True)
    assert re.search(r'Usage: srun (?:\[-{1,2}.*?\]\s+)+executable \[args\.\.\.\]$', output) is not None
