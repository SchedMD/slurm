############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re

import pytest

import atf


def test_usage():
    """Verify squeue --usage has the correct format"""

    output = atf.run_command_output("squeue --usage", fatal=True)
    assert re.search(r"Usage: squeue(?:\s+\[-{1,2}[^\]]+\])+$", output) is not None


@pytest.mark.skipif(
    atf.get_version("bin/squeue") < (26, 11),
    reason="Ticket 25236: squeue -J/--job-name was added in 26.11",
)
def test_usage_job_name():
    """Verify squeue --usage advertises -J"""

    output = atf.run_command_output("squeue --usage", fatal=True)

    assert "[-J job_names]" in output, "squeue --usage should advertise -J job_names"


@pytest.mark.skipif(
    atf.get_version("bin/squeue") < (26, 11),
    reason="Ticket 25236: squeue --usage was corrected to --jobs in 26.11",
)
def test_usage_jobs_spelling():
    """Verify squeue --usage advertises the plural --jobs"""

    output = atf.run_command_output("squeue --usage", fatal=True)

    assert "[--jobs jobids]" in output, "squeue --usage should advertise --jobs jobids"
