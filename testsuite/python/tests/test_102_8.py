############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

# Global variables
qos1 = "qos1"
qos2 = "qos2"
qos3 = "qos3"
acct1 = "acct1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_accounting(True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    # Create test QOS and account
    atf.run_command(
        f"sacctmgr -i add qos {qos1},{qos2},{qos3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos1},{qos2},{qos3}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_qos_removal_from_comma_separated():
    """Test QOS removal between from comma separated QOSes."""

    # Add qos to acct
    atf.run_command(
        f"sacctmgr -i modify account {acct1} set qos={qos1},{qos2},{qos3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Now del qos
    atf.run_command(
        f"sacctmgr -i remove qos {qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Check remaining qos
    output = atf.run_command_output(
        f"sacctmgr -ns show account {acct1} format=qos",
        fatal=True,
    )
    assert re.search(
        rf"{qos1},{qos3}", output
    ), f"Verify {qos2} is not in {acct1}, but {qos1},{qos2} are."


def test_qos_removal_from_incremental_comma_separated():
    """Test QOS removal between from incremental comma separated QOSes."""

    # Add qos to acct1
    atf.run_command(
        f"sacctmgr -i modify account {acct1} set qos+={qos1},{qos2},{qos3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Now del qos
    atf.run_command(
        f"sacctmgr -i remove qos {qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Check remaining qos
    output = atf.run_command_output(
        f"sacctmgr -ns show account {acct1} format=qos",
        fatal=True,
    )
    assert re.search(
        rf"{qos1},{qos3}", output
    ), f"Verify {qos2} is not in {acct1}, but {qos1},{qos2} are."
