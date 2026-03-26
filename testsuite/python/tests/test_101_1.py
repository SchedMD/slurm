############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf
import re


@pytest.fixture(scope="module", autouse=True)
def setup():
    if atf.get_version("bin/sacct") < (25, 5):
        atf.require_accounting()
        atf.require_slurm_running()


@pytest.mark.parametrize("opt", ["--help", "-h"])
def test_help(opt):
    """Verify sacct --help displays the help page"""

    output = atf.run_command_output("sacct --help", fatal=True)

    assert re.search(r"sacct \[<OPTION>\]", output) is not None
    assert re.search(r"Valid <OPTION> values are:", output) is not None
    assert re.search(r"-A, --accounts:", output) is not None
    assert re.search(r"-j, --jobs:", output) is not None
    assert re.search(r"-s, --state:", output) is not None
    assert re.search(r"-S, --starttime:", output) is not None
    assert re.search(r"valid start/end time formats are...", output) is not None


@pytest.mark.parametrize("opt", ["--helpformat", "-e"])
def test_helpformat(opt):
    """Verify sacct --helpformat displays the expected fields"""

    output = atf.run_command_output("sacct --helpformat", fatal=True)

    assert re.search(r"Account", output) is not None
    assert re.search(r"ExitCode", output) is not None
    assert re.search(r"JobID", output) is not None
    assert re.search(r"NodeList", output) is not None
    assert re.search(r"ReqMem", output) is not None
    assert re.search(r"Start", output) is not None
    assert re.search(r"State", output) is not None
    assert re.search(r"Timelimit", output) is not None
    assert re.search(r"User", output) is not None
    assert re.search(r"WorkDir", output) is not None
