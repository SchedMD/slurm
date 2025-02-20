############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re


def test_help():
    """Verify sacct --help displays the help page"""

    output = atf.run_command_output("sacct --help", fatal=True)

    assert re.search(r"sacct \[<OPTION>\]", output) is not None
    assert re.search(r"Valid <OPTION> values are:", output) is not None
    assert re.search(r"-A, --accounts:", output) is not None
    assert re.search(r"-j, --jobs:", output) is not None
    assert re.search(r"-s, --state:", output) is not None
    assert re.search(r"-S, --starttime:", output) is not None
    assert re.search(r"valid start/end time formats are...", output) is not None
