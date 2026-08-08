############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import pytest

import atf


def test_usage():
    """Verify scancel --usage has the correct format"""

    output = atf.run_command_output("scancel --usage", fatal=True)
    assert (
        re.search(r"Usage: scancel(?:\s+\[-{1,2}[^\]]+\])+\s+\[job_id.+\]$", output)
        is not None
    )


@pytest.mark.skipif(
    atf.get_version("bin/scancel") < (26, 11),
    reason="Ticket 25236: scancel -J/--job-name was added in 26.11",
)
def test_usage_job_name():
    """Verify scancel --usage advertises -J"""

    output = atf.run_command_output("scancel --usage", fatal=True)

    assert "[-J job_name]" in output, "scancel --usage should advertise -J job_name"
