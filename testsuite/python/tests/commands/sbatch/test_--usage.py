############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sbatch --usage has the correct format"""

    output = atf.run_command_output(f"sbatch --usage", fatal=True)
    assert re.search(r'Usage: sbatch (?:\[-{1,2}.*?\]\s+)+executable \[args\.\.\.\]$', output) is not None
