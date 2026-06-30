############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import atf


def test_usage():
    """Verify srun --usage looks reasonable"""

    output = atf.run_command_output("srun --usage", fatal=True)
    assert (
        re.search(
            r"Usage: srun (?:\[-{1,2}.*?\]\s+)+executable \[args\.\.\.\]$", output
        )
        is not None
    )
