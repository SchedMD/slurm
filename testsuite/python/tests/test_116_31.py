############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_help():
    """Verify srun --help displays the help page"""

    output = atf.run_command_output(f"srun --help", fatal=True)

    assert re.search(r'Usage: srun .*?OPTIONS.*?executable.*?args', output) is not None
    assert re.search(r'-b, --begin=time', output) is not None
    assert re.search(r'-N, --nodes=N', output) is not None
    assert re.search(r'-q, --qos=qos', output) is not None
    assert re.search(r'--mincpus=n', output) is not None
    assert re.search(r'-G, --gpus=n', output) is not None
