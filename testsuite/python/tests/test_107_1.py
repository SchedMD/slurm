############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import atf


def test_help():
    """Verify scancel --help displays the help page"""

    output = atf.run_command_output("scancel --help", fatal=True)

    assert re.search(r"Usage: scancel \[OPTIONS\]", output) is not None
    assert re.search(r"Help options:", output) is not None
    # TODO: Ticket 25236 - remove once 26.11 is the oldest supported version
    if atf.get_version("bin/scancel") >= (26, 11):
        assert (
            "-J, --job-name" in output
        ), "scancel --help should advertise -J, --job-name (ticket 25236)"
