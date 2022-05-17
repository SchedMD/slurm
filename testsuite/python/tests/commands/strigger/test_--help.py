############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_help():
    """Verify strigger --help displays the help page"""

    output = atf.run_command_output(f"strigger --help", fatal=True)

    assert re.search(r'strigger \[--set \| --get \| --clear\] \[OPTIONS\]', output) is not None
    assert re.search(r'--set +create a trigger', output) is not None
    assert re.search(r'--get +get trigger information', output) is not None
    assert re.search(r'--clear +delete a trigger', output) is not None
    assert re.search(r'-j, --jobid=', output) is not None
    assert re.search(r'-n, --node\[=host\]', output) is not None
    assert re.search(r'-v, --verbose +print detailed', output) is not None
