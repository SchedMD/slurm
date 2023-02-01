############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re


def test_help():
    """Verify scontrol --help displays the help page"""

    output = atf.run_command_output(f"scontrol --help", fatal=True)

    assert re.search(r'Valid <COMMAND> values are:', output) is not None
    assert re.search(r'reconfigure', output) is not None
    assert re.search(r'show <ENTITY> \[<ID>\]', output) is not None
    assert re.search(r'version', output) is not None
