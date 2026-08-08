############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import atf


def test_help():
    """Verify squeue --help displays the help page"""

    output = atf.run_command_output("squeue --help", fatal=True)

    assert re.search(r"Usage: squeue \[OPTIONS\]", output) is not None
    assert re.search(r"Help options:", output) is not None
    # TODO: Ticket 25236 - remove once 26.11 is the oldest supported version
    if atf.get_version("bin/squeue") >= (26, 11):
        assert (
            "-J, --job-name" in output
        ), "squeue --help should advertise -J, --job-name (ticket 25236)"
