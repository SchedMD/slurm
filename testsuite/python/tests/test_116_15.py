############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_version():
    """Verify srun --version looks reasonable"""

    output = atf.run_command_output(f"srun --version", fatal=True)
    assert re.search(r'slurm \d+\.\d+\.\d+', output) is not None
