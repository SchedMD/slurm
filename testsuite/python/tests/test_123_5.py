############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os
import pytest
import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
qos1 = f"{test_name}_qos"
acct1 = f"{test_name}_acct"
res_name = f"{test_name}_resv"
testuser = atf.properties["test-user"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    global local_cluster_name

    atf.require_version(
        (25, 11),
        "bin/scontrol",
        reason="Creating reservations with qos= added in scontrol 25.11",
    )
    atf.require_accounting(True)
    atf.require_slurm_running()
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    local_cluster_name = atf.get_config_parameter("ClusterName")

    # Create test QOS and User that can use it
    atf.run_command(
        f"sacctmgr -i add qos {qos1}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i add account {acct1} cluster={local_cluster_name}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i add user {testuser} cluster={local_cluster_name} account={acct1} qos=normal,{qos1}",
        user=atf.properties["slurm-user"],
    )

    atf.run_command(
        f"sacctmgr -i add user {testuser} cluster={local_cluster_name} wckey={acct1}",
        user=atf.properties["slurm-user"],
    )
    # Create the reservation for QOS test
    result = atf.run_command(
        f"scontrol create reservationname={res_name} qos={qos1} start=now duration=1 nodecnt=1",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, "Couldn't create the reservation!"

    yield

    atf.run_command(
        f"scontrol delete reservation {res_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove user {testuser} wckey={acct1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove user {testuser} account={acct1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_reservation_qos():
    """Test that a reservation created for QOS {qos1} can't be used by atf"""

    # Try to run a job as in the wrong QOS
    result = atf.run_command(
        f"srun -N1 --reservation={res_name} --account={acct1} --qos=normal true",
    )
    assert (
        result["exit_code"] != 0
    ), "The job should have been denied! {result[exit_code]}"
    assert (
        "Problem using reservation" in result["stderr"]
    ), "The job should have been denied!"

    # Try to run a job as in the correct QOS
    result = atf.run_command(
        f"srun -N1 --reservation={res_name} --account={acct1} --qos=normal,{qos1} true",
    )
    assert (
        result["exit_code"] == 0
    ), "ExitCode wasn't 0. The job should not have been denied!"
    assert (
        "Problem using reservation" not in result["stderr"]
    ), "The job should not have been denied for user!"
