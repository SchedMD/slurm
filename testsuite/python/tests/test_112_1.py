############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_slurm_running()


def test_help():
    """Verify -h is a valid option"""

    assert atf.run_command_exit("slurmrestd -h") == 0


def test_invalid_option():
    """Verify failure for invalid option"""

    assert atf.run_command_exit("slurmrestd --invalid") != 0


def test_a():
    """Check -a option"""

    assert atf.run_command_exit("slurmrestd -a list") == 0
    assert atf.run_command_exit("slurmrestd -a invalid_plugin") != 0


def test_s():
    """Check -s option"""

    assert atf.run_command_exit("slurmrestd -s list") == 0
    assert atf.run_command_exit("slurmrestd -s invalid_plugin") != 0


def test_get_a_rest_auth():
    """Check response to GET with -a rest_auth"""

    assert atf.run_command_exit("slurmrestd -a rest_auth/local", input="GET /openapi HTTP/1.1\r\nConnection: Close\r\n\r\n") == 0
    assert atf.run_command_exit("slurmrestd -a rest_auth/invalid", input="GET /openapi HTTP/1.1\r\nConnection: Close\r\n\r\n") != 0


# Subtests related to bug 10388

def test_invalid_input():
    """Check response to invalid input (bug 10388)"""

    assert atf.run_command_exit("slurmrestd", input="INVALID") != 0
    assert atf.run_command_exit("slurmrestd", input="INVALID\r\n\r\n") != 0
    assert atf.run_command_exit("slurmrestd", input="INVALID\r\nINVALID\r\n\r\n") != 0
    assert atf.run_command_exit("slurmrestd", input="GET /openapi HTTP/1.1\r\nINVALID\r\n\r\n") != 0
