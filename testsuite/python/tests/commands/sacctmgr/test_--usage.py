############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sacctmgr --usage displays the help page"""

    output = atf.run_command_output(f"sacctmgr --usage", fatal=True)

    assert re.search(r'sacctmgr \[<OPTION>\] \[<COMMAND>\]', output) is not None
    assert re.search(r'Valid <OPTION> values are:', output) is not None
    assert re.search(r'Valid <COMMAND> values are:', output) is not None
    assert re.search(r'<ENTITY> may be', output) is not None
    assert re.search(r'<SPECS> are different for each command entity pair', output) is not None
    assert re.search(r'add account', output) is not None
    assert re.search(r'list associations', output) is not None
    assert re.search(r'modify cluster', output) is not None
    assert re.search(r'delete qos', output) is not None
